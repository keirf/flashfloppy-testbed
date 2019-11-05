/*
 * floppy_generic.c
 * 
 * Generic floppy drive low-level support routines.
 * Mainly dealing with IRQs, timers and DMA.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

/* A DMA buffer for running a timer associated with a floppy-data I/O pin. */
static struct dma_ring {
    /* Indexes into the buf[] ring buffer. */
    uint16_t cons; /* dma_rd: our consumer index for flux samples */
    union {
        uint16_t prod; /* dma_wr: our producer index for flux samples */
        uint16_t prev_sample; /* dma_rd: previous CCRx sample value */
    };
    /* DMA ring buffer of timer values (ARR or CCRx). */
    uint16_t buf[1024];
} dma;


/*
 * READ PATH
 */

static bool_t rdata_wait_sync(struct read *rd)
{
    const uint16_t buf_mask = ARRAY_SIZE(dma.buf) - 1;
    uint16_t cons, prod, prev = dma.prev_sample, curr, next;
    uint16_t cell = cur_drive->ticks_per_cell;
    uint16_t window = cell + (cell >> 1);
    uint32_t bc_dat = rd->bc_window, bc_prod = rd->bc_prod;
    unsigned int sync = rd->sync, sync_found = 0;
    uint32_t *bc_buf = rd->p;

    /* Find out where the DMA engine's producer index has got to. */
    prod = ARRAY_SIZE(dma.buf) - dma_rdata.cndtr;

    /* Process the flux timings into the raw bitcell buffer. */
    for (cons = dma.cons; cons != prod; cons = (cons+1) & buf_mask) {
        if (sync_found)
            break;
        next = dma.buf[cons];
        curr = next - prev;
#ifndef QUICKDISK /* This happens in ibm_mfm_{scan,search} for example. */
        if (curr > (6*cell)) {
            printk("Long flux @ dma=%u bc=%u: %u-%u=%u / %u\n",
                   cons, bc_prod, next, prev, curr, cell);
            WARN_ON(TRUE);
        }
#endif
        prev = next;
        while (curr > window) {
            curr -= cell;
            bc_dat <<= 1;
            bc_prod++;
        }
        bc_dat = (bc_dat << 1) | 1;
        bc_prod++;
        switch (sync) {
        case SYNC_fm:
            /* FM clock sync clock byte is 0xc7. Check for:
             * 1010 1010 1010 1010 1x1x 0x0x 0x1x 1x1x */
            if ((bc_dat & 0xffffd555) == 0x55555015)
                sync_found = 31;
            break;
        case SYNC_mfm:
            if (bc_dat == 0x44894489)
                sync_found = 32;
            break;
        }
    }

    /* Save our progress for next time. */
    rd->bc_window = bc_dat;
    rd->bc_prod = bc_prod;
    dma.cons = cons;
    dma.prev_sample = prev;

    if (sync_found != 0) {
        time_t now = time_now();
        cell = ((now - rd->start) << 4) / bc_prod;
        rd->start = now - ((sync_found * cell) >> 4);
        rd->bc_prod = sync_found;
        bc_buf[0] = htobe32(bc_dat);
        return TRUE;
    }

    return FALSE;
}

static bool_t rdata_flux_to_bc(struct read *rd)
{
    const uint16_t buf_mask = ARRAY_SIZE(dma.buf) - 1;
    uint16_t cons, prod, prev = dma.prev_sample, curr, next;
    uint16_t cell = cur_drive->ticks_per_cell;
    uint16_t window = cell + (cell >> 1);
    uint32_t bc_dat = rd->bc_window, bc_prod = rd->bc_prod;
    uint32_t bc_max = rd->nr_words * 16;
    uint32_t *bc_buf = rd->p;

    /* Find out where the DMA engine's producer index has got to. */
    prod = ARRAY_SIZE(dma.buf) - dma_rdata.cndtr;

    /* Process the flux timings into the raw bitcell buffer. */
    for (cons = dma.cons; cons != prod; cons = (cons+1) & buf_mask) {
        next = dma.buf[cons];
        curr = next - prev;
        if (curr > (6*cell)) {
            printk("Long flux @ dma=%u bc=%u: %u-%u=%u / %u\n",
                   cons, bc_prod, next, prev, curr, cell);
            WARN_ON(TRUE);
        }
        prev = next;
        while (curr > window) {
            curr -= cell;
            bc_dat <<= 1;
            if (!(++bc_prod&31)) {
                bc_buf[(bc_prod-1) / 32] = htobe32(bc_dat);
                if (bc_prod == bc_max)
                    return TRUE;
            }
        }
        bc_dat = (bc_dat << 1) | 1;
        if (!(++bc_prod&31)) {
            bc_buf[(bc_prod-1) / 32] = htobe32(bc_dat);
            if (bc_prod == bc_max)
                return TRUE;
        }
    }

    /* Save our progress for next time. */
    rd->bc_window = bc_dat;
    rd->bc_prod = bc_prod;
    dma.cons = cons;
    dma.prev_sample = prev;
    return FALSE;
}

void floppy_read_prep(struct read *rd)
{
    /* Check buffer alignment. */
    ASSERT(((uint32_t)rd->p & 3) == 0);
    ASSERT((rd->nr_words & 1) == 0);

    /* ~0 avoids sync match within fewer than 32 bits of scan start. */
    rd->bc_window = ~0;
    rd->bc_prod = 0;

    /* Start DMA. */
    dma_rdata.cndtr = ARRAY_SIZE(dma.buf);
    dma_rdata.ccr = (DMA_CCR_PL_HIGH |
                     DMA_CCR_MSIZE_16BIT |
                     DMA_CCR_PSIZE_16BIT |
                     DMA_CCR_MINC |
                     DMA_CCR_CIRC |
                     DMA_CCR_DIR_P2M |
                     DMA_CCR_EN);

    /* DMA soft state. */
    dma.cons = 0;
    dma.prev_sample = tim_rdata->cnt;
}

void floppy_read(struct read *rd)
{
    /* Wait for RDATA active. */
    exti->pr = m(pin_rdata);
    while (!(exti->pr & m(pin_rdata)))
        continue;

    /* Start timer. */
#ifdef QUICKDISK
    tim_rdata->ccer = TIM_CCER_CC1E; /* positive pulses, leading edge */
#else
    tim_rdata->ccer = TIM_CCER_CC1E | TIM_CCER_CC1P; /* negative pulses */
#endif
    tim_rdata->cr1 = TIM_CR1_CEN;

    rd->start = time_now();

    if (rd->sync != 0) {
        while (!rdata_wait_sync(rd))
            continue;
    }

    while (!rdata_flux_to_bc(rd))
        continue;

    rd->end = time_now();

    /* Turn off timer. */
    tim_rdata->ccer = 0;
    tim_rdata->cr1 = 0;
    tim_rdata->sr = 0; /* dummy, drains any pending DMA */

    /* Turn off DMA. */
    dma_rdata.ccr = 0;
}


/*
 * WRITE PATH
 */

static uint16_t _wdata_bc_to_flux(
    struct write *wr, uint16_t *tbuf, uint16_t nr)
{
    uint32_t ticks_per_cell = cur_drive->ticks_per_cell << 4;
    uint32_t ticks = wr->ticks_since_flux;
    uint32_t x, y = 32, todo = nr;
    const uint32_t *bc_b = wr->p;
    uint32_t bc_c = wr->bc_cons, bc_p = wr->nr_words * 16;

    if (todo == 0)
        return 0;

    /* Convert pre-generated bitcells into flux timings. */
    while (bc_c != bc_p) {
        y = bc_c % 32;
        x = be32toh(bc_b[bc_c / 32]) << y;
        bc_c += 32 - y;
        while (y < 32) {
            y++;
            ticks += ticks_per_cell;
            if ((int32_t)x < 0) {
                *tbuf++ = (ticks >> 4) - 1;
                ticks &= 15;
                if (!--todo)
                    goto out;
            }
            x <<= 1;
        }
    }

    ASSERT(y == 32);

out:
    wr->bc_cons = bc_c - (32 - y);
    wr->ticks_since_flux = ticks;

    return nr - todo;
}

static void wdata_bc_to_flux(struct write *wr)
{
    const uint16_t buf_mask = ARRAY_SIZE(dma.buf) - 1;
    uint16_t nr_to_wrap, nr_to_cons, nr, dmacons;

    /* Find out where the DMA engine's consumer index has got to. */
    dmacons = ARRAY_SIZE(dma.buf) - dma_wdata.cndtr;

    /* Find largest contiguous stretch of ring buffer we can fill. */
    nr_to_wrap = ARRAY_SIZE(dma.buf) - dma.prod;
    nr_to_cons = (dmacons - dma.prod - 1) & buf_mask;
    nr = min(nr_to_wrap, nr_to_cons);

    /* Now attempt to fill the contiguous stretch with flux data calculated 
     * from buffered bitcell data. */
    dma.prod += _wdata_bc_to_flux(wr, &dma.buf[dma.prod], nr);
    dma.prod &= buf_mask;
}

void floppy_write_prep(struct write *wr)
{
    /* Check buffer alignment. */
    ASSERT(((uint32_t)wr->p & 3) == 0);
    ASSERT((wr->nr_words & 1) == 0);

    wr->ticks_since_flux = 0;
    wr->bc_cons = 0;

    /* Initialise DMA ring indexes (consumer index is implicit). */
    dma_wdata.cndtr = ARRAY_SIZE(dma.buf);
    dma.prod = 0;

    /* Generate initial flux values. */
    wdata_bc_to_flux(wr);

    /* Enable DMA only after flux values are generated. */
    dma_wdata.ccr = (DMA_CCR_PL_HIGH |
                     DMA_CCR_MSIZE_16BIT |
                     DMA_CCR_PSIZE_16BIT |
                     DMA_CCR_MINC |
                     DMA_CCR_CIRC |
                     DMA_CCR_DIR_M2P |
                     DMA_CCR_EN);
}

void floppy_write(struct write *wr)
{
    uint32_t bc_max = wr->nr_words * 16;
    uint16_t dmacons, todo, prev_todo;
    unsigned int index_count = index.count;

    /* Start timer. */
    tim_wdata->egr = TIM_EGR_UG;
    tim_wdata->sr = 0; /* dummy write, gives h/w time to process EGR.UG=1 */
    tim_wdata->cr1 = TIM_CR1_CEN;

    /* Enable output. */
    gpio_configure_pin(gpio_data, pin_wdata, AFO_bus);
    assert_wgate();

    /* Emit flux into the DMA ring until all bitcells are consumed. */
    while (wr->bc_cons != bc_max) {
        wdata_bc_to_flux(wr);
        /* Early termination on index pulse? */
        if (wr->terminate_at_index && (index.count != index_count))
            goto out;
    }

    /* Wait for DMA ring to drain. */
    todo = ~0;
    do {
        /* Early termination on index pulse? */
        if (wr->terminate_at_index && (index.count != index_count))
            goto out;
        /* Check progress of draining the DMA ring. */
        prev_todo = todo;
        dmacons = ARRAY_SIZE(dma.buf) - dma_wdata.cndtr;
        todo = (dma.prod - dmacons) & (ARRAY_SIZE(dma.buf) - 1);
    } while ((todo != 0) && (todo <= prev_todo));

out:
    /* Turn off the output pin */
    deassert_wgate();
    gpio_configure_pin(gpio_data, pin_wdata, GPO_bus);

    /* Turn off timer. */
    tim_wdata->cr1 = 0;
    tim_wdata->sr = 0; /* dummy, drains any pending DMA */

    /* Turn off DMA. */
    dma_wdata.ccr = 0;
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
