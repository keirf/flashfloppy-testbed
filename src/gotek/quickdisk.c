/*
 * gotek/quickdisk.c
 * 
 * Gotek-specific Quick Disk interface setup.
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
#define pin_media  1 /* PA1 */
#define pin_ready  4 /* PB4 */
#define get_wrprot()  gpio_read_pin(gpiob, pin_wrprot)
#define get_media()   gpio_read_pin(gpioa, pin_media)
#define get_ready()   gpio_read_pin(gpiob, pin_ready)

/* Output pins. */
#define set_reset(x) gpio_write_pin(gpiob, 8, x)
#define set_wgate(x) gpio_write_pin(gpiob, 5, x)
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
void IRQ_10(void) __attribute__((alias("IRQ_READY_changed"))); /* EXTI4 */
static const struct exti_irq exti_irqs[] = {
    { 10, FLOPPY_IRQ_INDEX_PRI },
};

static void board_floppy_init(void)
{
    /* Input pins. */
    gpio_configure_pin(gpiob, pin_wrprot, GPI_bus);
    gpio_configure_pin(gpioa, pin_media,  GPI_bus);
    gpio_configure_pin(gpiob, pin_ready,  GPI_bus);

    /* Output pins, buffered via 74HC inverter and open-drain MOSFET. */
    gpio_configure_pin(gpiob, 8, GPO_pushpull(_2MHz,O_FALSE));
    gpio_configure_pin(gpiob, 5, GPO_pushpull(_2MHz,O_TRUE));

    /* Output pins, unbuffered. */
    gpio_configure_pin(gpiob, 1, GPO_opendrain(_2MHz,O_FALSE));

    /* PA[15:5,3:0] -> EXT[15:5,3:0] ; PB[4] -> EXT[4] */
    afio->exticr1 = afio->exticr3 = afio->exticr4 = 0x0000;
    afio->exticr2 = 0x0001;

    exti->imr = exti->ftsr = exti->rtsr = m(pin_rdata) | m(pin_ready);
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
