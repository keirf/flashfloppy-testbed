/*
 * floppy.c
 * 
 * Floppy interface control.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#define GPI_bus GPI_floating
#define GPO_bus GPO_pushpull(_2MHz,O_FALSE)
#define AFO_bus (AFO_pushpull(_2MHz) | (O_FALSE<<4))

#define m(bitnr) (1u<<(bitnr))

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

struct exti_irq {
    uint8_t irq, pri;
};

#include "gotek/floppy.c"

static struct drive drive[2];
static uint8_t cur_unit;
struct drive *cur_drive;

static void step_one_out(void)
{
    set_dir(O_FALSE);
    set_step(O_TRUE);
    set_step(O_FALSE);
    delay_ms(2);
}

static void step_one_in(void)
{
    set_dir(O_TRUE);
    set_step(O_TRUE);
    set_step(O_FALSE);
    delay_ms(2);
}

void floppy_init(void)
{
    const struct exti_irq *e;
    unsigned int i;

    board_floppy_init();

    gpio_configure_pin(gpio_data, pin_rdata, GPI_bus);
    gpio_configure_pin(gpio_data, pin_wdata, GPO_bus);

    /* Configure physical interface interrupts. */
    for (i = 0, e = exti_irqs; i < ARRAY_SIZE(exti_irqs); i++, e++) {
        IRQx_set_prio(e->irq, e->pri);
        IRQx_set_pending(e->irq);
        IRQx_enable(e->irq);
    }

    /* Find cylinder 0 on unit 0. */
    set_sel0(O_TRUE);
    delay_us(10);
    while (get_trk0() != O_TRUE)
        step_one_out();
    set_sel0(O_FALSE);
    drive[0].cyl = 0;

    /* RDATA Timer setup: 
     * The counter runs from 0x0000-0xFFFF inclusive at full SYSCLK rate.
     *  
     * Ch.1 (RDATA) is in Input Capture mode, sampling on every clock and with
     * no input prescaling or filtering. Samples are captured on the falling 
     * edge of the input (CCxP=1). DMA is used to copy the sample into a ring
     * buffer for batch processing in the DMA-completion ISR. */
    tim_rdata->psc = 0;
    tim_rdata->arr = 0xffff;
    tim_rdata->ccmr1 = TIM_CCMR1_CC1S(TIM_CCS_INPUT_TI1);
    tim_rdata->dier = TIM_DIER_CC1DE;
    tim_rdata->cr2 = 0;

    /* RDATA DMA setup: From the RDATA Timer's CCRx into a circular buffer. */
    dma_rdata.cpar = (uint32_t)(unsigned long)&tim_rdata->ccr1;
    dma_rdata.cmar = (uint32_t)(unsigned long)dma.buf;

    /* WDATA Timer setup:
     * The counter is incremented at full SYSCLK rate. 
     *  
     * Ch.2 (WDATA) is in PWM mode 1. It outputs O_TRUE for 400ns and then 
     * O_FALSE until the counter reloads. By changing the ARR via DMA we alter
     * the time between (fixed-width) O_TRUE pulses, mimicking floppy drive 
     * timings. */
    tim_wdata->psc = 0;
    tim_wdata->ccmr1 = (TIM_CCMR1_CC2S(TIM_CCS_OUTPUT) |
                        TIM_CCMR1_OC2M(TIM_OCM_PWM1));
    tim_wdata->ccer = TIM_CCER_CC2E | ((O_TRUE==0) ? TIM_CCER_CC2P : 0);
    tim_wdata->ccr2 = sysclk_ns(400);
    tim_wdata->dier = TIM_DIER_UDE;
    tim_wdata->cr2 = 0;

    /* WDATA DMA setup: From a circular buffer into the WDATA Timer's ARR. */
    dma_wdata.cpar = (uint32_t)(unsigned long)&tim_wdata->arr;
    dma_wdata.cmar = (uint32_t)(unsigned long)dma.buf;
}

void floppy_select(unsigned int unit)
{
    struct drive *drv = &drive[unit];

    /* Remember which unit is now selected. */
    ASSERT(unit <= 1);
    cur_unit = unit;
    cur_drive = drv;

    /* Select the requested unit. */
    if (unit) {
        set_sel0(O_FALSE);
        set_sel1(O_TRUE);
    } else {
        set_sel1(O_FALSE);
        set_sel0(O_TRUE);
    }

    /* Wait for inputs to settle. */
    delay_us(10);
    set_motor(O_TRUE);
}

void floppy_seek(unsigned int cyl, unsigned int side)
{
    struct drive *drv = cur_drive;

    /* Select requested disk side. */
    set_side(side ? O_TRUE : O_FALSE);

    /* Special handling for cylinder 0. */
    if (cyl == 0) {
        while (get_trk0() != O_TRUE)
            step_one_out();
        drv->cyl = 0;
    }

    /* Seek to the requested cylinder and wait for settle. */
    while (drv->cyl < cyl) {
        step_one_in();
        drv->cyl++;
    }
    while (drv->cyl > cyl) {
        step_one_out();
        drv->cyl--;
    }
    delay_ms(10);
}

void floppy_disk_change(void)
{
    struct drive *drv = cur_drive;
    time_t t[4];

    /* Reset DSKCHG counter. */
    dskchg.count = 0;

    /* Seek from D-A back to cylinder 0. This triggers the disk change. */
    BUG_ON(get_trk0() == O_TRUE);
    floppy_seek(0, 0);

    /* Wait for DSKCHG to toggle. */
    t[0] = time_now();
    while (dskchg.count == 0)
        BUG_ON(time_diff(t[0], time_now()) > time_ms(1000));

    /* Wait for image to be mounted. We step heads and poll DSKCHG. */ 
    t[1] = time_now();
    while (get_dskchg() == O_TRUE) {
        BUG_ON(time_diff(t[1], time_now()) > time_ms(5000));
        step_one_out();
        if (drv->cyl)
            drv->cyl--;
    }

    /* Wait for motor spin up. */
    set_motor(O_TRUE);
    t[2] = time_now();
    while (get_ready() == O_FALSE)
        BUG_ON(time_diff(t[2], time_now()) > time_ms(1000));
    t[3] = time_now();

    if (time_diff(t[0],t[3]) > time_ms(1000))
        printk("WARN: Long Disk Change: Eject=%ums Insert=%ums Ready=%ums\n",
               time_diff(t[0],t[1]) / time_ms(1),
               time_diff(t[1],t[2]) / time_ms(1),
               time_diff(t[2],t[3]) / time_ms(1));
}

void test_ready(void)
{
    time_t t;
    set_motor(O_FALSE);
    t = time_now();
    while ((get_ready() == O_TRUE)
           && time_diff(t, time_now()) < time_ms(500))
        continue;
    if (get_ready() == O_TRUE) {
        set_motor(O_TRUE);
        printk("MOTOR ignored\n");
        return;
    }
    printk("Motor-to-RDY: OFF=%uus, ", time_diff(t, time_now()) / time_us(1));
    set_motor(O_TRUE);
    t = time_now();
    while (get_ready() == O_FALSE)
        continue;
    printk("ON=%ums\n", (time_diff(t, time_now()) + time_us(500))
           / time_ms(1));
}


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
        if (curr > (6*cell)) {
            printk("Long flux @ dma=%u bc=%u: %u-%u=%u / %u\n",
                   cons, bc_prod, next, prev, curr, cell);
            WARN_ON(TRUE);
        }
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
    tim_rdata->ccer = TIM_CCER_CC1E | TIM_CCER_CC1P;
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
    set_wgate(O_TRUE);

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
    set_wgate(O_FALSE);
    gpio_configure_pin(gpio_data, pin_wdata, GPO_bus);

    /* Turn off timer. */
    tim_wdata->cr1 = 0;
    tim_wdata->sr = 0; /* dummy, drains any pending DMA */

    /* Turn off DMA. */
    dma_wdata.ccr = 0;
}


/*
 * INTERRUPT HANDLERS
 */

volatile struct index index;
volatile struct dskchg dskchg;

static void IRQ_INDEX_changed(void)
{
    /* Clear INDEX-changed flag. */
    exti->pr = m(pin_index);

    index.count++;
    index.timestamp = time_now();
}

static void IRQ_DSKCHG_changed(void)
{
    /* Clear DSKCHG-changed flag. */
    exti->pr = m(pin_dskchg);

    dskchg.count++;
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
