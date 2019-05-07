/*
 * gotek/floppy.c
 * 
 * Gotek-specific floppy-interface setup.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#define O_FALSE 1
#define O_TRUE  0

/* Input pins */
#define pin_wrprot 0 /* PB0 */
#define pin_dskchg 1 /* PA1 */
#define pin_index  0 /* PA0 */
#define pin_trk0   9 /* PB9 */
#define pin_ready  4 /* PB4 */
#define get_wrprot()  gpio_read_pin(gpiob, pin_wrprot)
#define get_dskchg()  gpio_read_pin(gpioa, pin_dskchg)
#define get_index()   gpio_read_pin(gpioa, pin_index)
#define get_trk0()    gpio_read_pin(gpiob, pin_trk0)
#define get_ready()   gpio_read_pin(gpiob, pin_ready)

/* Output pins. */
#define set_dir(x)   gpio_write_pin(gpiob, 7, x)
#define set_step(x)  gpio_write_pin(gpiob, 8, x)
#define set_sel0(x)  gpio_write_pin(gpiob, 6, x)
#define set_wgate(x) gpio_write_pin(gpiob, 5, x)
#define set_side(x)  gpio_write_pin(gpiob, 3, x)
#define set_sel1(x)  gpio_write_pin(gpioa, 2, x)
#define set_motor(x) gpio_write_pin(gpiob, 1, x)

#define gpio_data gpioa

#define pin_rdata   8
#define tim_rdata   (tim1)
#define dma_rdata   (dma1->ch2)
#define dma_rdata_ch 2

#define pin_wdata   7
#define tim_wdata   (tim3)
#define dma_wdata   (dma1->ch3)
#define dma_wdata_ch 3

/* EXTI IRQs. */
void IRQ_6(void) __attribute__((alias("IRQ_INDEX_changed"))); /* EXTI0 */
void IRQ_7(void) __attribute__((alias("IRQ_DSKCHG_changed"))); /* EXTI1 */
/*void IRQ_10(void) __attribute__((alias("IRQ_READY_changed")));*/ /* EXTI4 */
/*void IRQ_23(void) __attribute__((alias("IRQ_TRK0_changed")));*/ /* EXTI9_5 */
static const struct exti_irq exti_irqs[] = {
    {  6, FLOPPY_IRQ_INDEX_PRI },
    {  7, FLOPPY_IRQ_DSKCHG_PRI },
};

static void board_floppy_init(void)
{
    /* Input pins. */
    gpio_configure_pin(gpiob, pin_wrprot, GPI_bus);
    gpio_configure_pin(gpioa, pin_dskchg, GPI_bus);
    gpio_configure_pin(gpioa, pin_index,  GPI_bus);
    gpio_configure_pin(gpiob, pin_trk0,   GPI_bus);
    gpio_configure_pin(gpiob, pin_ready,  GPI_bus);

    /* Output pins, buffered via 74HC inverter and open-drain MOSFET. */
    gpio_configure_pin(gpiob, 7, GPO_bus);
    gpio_configure_pin(gpiob, 8, GPO_bus);
    gpio_configure_pin(gpiob, 6, GPO_bus);
    gpio_configure_pin(gpiob, 5, GPO_bus);
    gpio_configure_pin(gpiob, 3, GPO_bus);

    /* Output pins, unbuffered. */
    gpio_configure_pin(gpioa, 2, GPO_opendrain(_2MHz,O_FALSE));
    gpio_configure_pin(gpiob, 1, GPO_opendrain(_2MHz,O_FALSE));

    /* PB[15:2] -> EXT[15:2], PA[1:0] -> EXT[1:0] */
    afio->exticr2 = afio->exticr3 = afio->exticr4 = 0x1111;
    afio->exticr1 = 0x1100;

    exti->imr = exti->ftsr = m(pin_dskchg) | m(pin_index);
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
