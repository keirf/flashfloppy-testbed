ENTRY(vector_table)

MEMORY
{
  FLASH (rx)      : ORIGIN = FLASH_BASE, LENGTH = FLASH_LEN
  RAM (rwx)       : ORIGIN = RAM_BASE, LENGTH = RAM_LEN
}
REGION_ALIAS("RO", FLASH);
REGION_ALIAS("RW", RAM);

SECTIONS
{
  .text : {
    _stext = .;
    *(.vector_table)
    _smaintext = .;
    *(.text)
    *(.text*)
    _emaintext = .;
    *(.rodata)
    *(.rodata*)
    KEEP (*(.init))
    KEEP (*(.fini))
    . = ALIGN(4);
    _etext = .;
  } >RO

  .data : AT (_etext) {
    . = ALIGN(4);
    _sdat = .;
    *(.data)
    *(.data*)
    . = ALIGN(4);
    _edat = .;
    _ldat = LOADADDR(.data);
  } >RW

  .bss : {
    . = ALIGN(8);
    _irq_stackbottom = .;
    . = . + 512;
    _irq_stacktop = .;
    _sbss = .;
    *(.bss)
    *(.bss*)
    . = ALIGN(8);
    _ebss = .;
    _thread_stackbottom = .;
  } >RW

  _thread_stacktop = RAM_BASE + RAM_LEN;

  /DISCARD/ : {
    *(.eh_frame)
  }

  .ARM.attributes 0 : { *(.ARM.attributes) }
}
