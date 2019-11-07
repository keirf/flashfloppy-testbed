/*
 * quickdisk.c
 * 
 * Quick Disk interface control.
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

struct exti_irq {
    uint8_t irq, pri;
};

#define assert_wgate() set_wgate(O_FALSE)
#define deassert_wgate() set_wgate(O_TRUE)

#include "gotek/quickdisk.c"
#include "floppy_generic.c"

static struct drive drive;
struct drive *cur_drive;

void floppy_init(void)
{
    const struct exti_irq *e;
    unsigned int i;

    board_floppy_init();

    cur_drive = &drive;
    drive.ticks_per_cell = sysclk_us(4) + 66; /* 4.917us */

    gpio_configure_pin(gpio_data, pin_rdata, GPI_bus);
    gpio_configure_pin(gpio_data, pin_wdata, GPO_bus);

    /* Configure physical interface interrupts. */
    for (i = 0, e = exti_irqs; i < ARRAY_SIZE(exti_irqs); i++, e++) {
        IRQx_set_prio(e->irq, e->pri);
        IRQx_set_pending(e->irq);
        IRQx_enable(e->irq);
    }

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
    tim_wdata->ccr2 = sysclk_ns(1500); /* 1.5us negative pulses */
    tim_wdata->dier = TIM_DIER_UDE;
    tim_wdata->cr2 = 0;

    /* WDATA DMA setup: From a circular buffer into the WDATA Timer's ARR. */
    dma_wdata.cpar = (uint32_t)(unsigned long)&tim_wdata->arr;
    dma_wdata.cmar = (uint32_t)(unsigned long)dma.buf;
}


/*
 * INTERRUPT HANDLERS
 */

volatile struct index index;

static void IRQ_READY_changed(void)
{
    /* Clear READY-changed flag. */
    exti->pr = m(pin_ready);

    if (!get_ready()) {
        index.count++;
        index.timestamp = time_now();
    }
}

/*
 * MAIN ROUTINES
 */

int EXC_reset(void) __attribute__((alias("main")));

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

static void check_mfm(void)
{
    const unsigned int n = 3, sz = 128 << n;
    struct idam idam[22];
    struct ibm_scan_info info[32];
    unsigned int gap3, seen_nr, i;
    uint8_t *p = alloca(sz), *q = alloca(sz);

    for (i = 0; i < ARRAY_SIZE(idam); i++) {
        idam[i].c = 2;
        idam[i].h = 3;
        idam[i].r = 4 + i;
        idam[i].n = n;
    }

    printk("Format... ");
    set_motor(O_TRUE);
    ibm_mfm_write_track(idam, ARRAY_SIZE(idam), 84);

    printk("Write Sector... ");
    for (i = 0; i < sz; i++)
        p[i] = rand()>>8;
    ibm_mfm_write_sector(p, &idam[3], 84);

    printk("Read Sector... ");
    ibm_mfm_read_sector(q, &idam[3]);
    WARN_ON(memcmp(p, q, sz));

    printk("Scan Track... ");
    seen_nr = ibm_mfm_scan(info, ARRAY_SIZE(info), &gap3);
    check_ibm_idams(idam, ARRAY_SIZE(idam), info, seen_nr);

    printk("Motor Off and Pause... ");
    set_motor(O_FALSE);
    delay_ms(5000);
    printk("Done.\n");
}

int main(void)
{
    unsigned int i;

    /* Relocate DATA. Initialise BSS. */
    if (_sdat != _ldat)
        memcpy(_sdat, _ldat, _edat-_sdat);
    memset(_sbss, 0, _ebss-_sbss);

    stm32_init();
    time_init();
    console_init();
    console_crash_on_input();
    board_init();
    delay_ms(200); /* 5v settle */

    printk("\n** FlashFloppy Quick Disk TestBed for Gotek\n");
    printk("** Keir Fraser <keir.xen@gmail.com>\n");
    printk("** https://github.com/keirf/FlashFloppy\n\n");

    floppy_init();

    led_7seg_init();
    led_7seg_write_string("QDT");

    for (i = 0; ; i++) {
        printk("\n*** ROUND %u ***\n", i);
        check_mfm();
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
