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

struct exti_irq {
    uint8_t irq, pri;
};

#define assert_wgate() set_wgate(O_TRUE)
#define deassert_wgate() set_wgate(O_FALSE)

#include "gotek/floppy.c"
#include "floppy_generic.c"

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
    dma_rdata.cmar = (uint32_t)(unsigned long)dma_r.buf;

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
    dma_wdata.cmar = (uint32_t)(unsigned long)dma_w.buf;
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
