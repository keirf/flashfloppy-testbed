# FlashFloppy TestBed

A testbed for the [FlashFloppy retro floppy emulator][Downloads].

Testbed runs on modified Gotek floppy drive, which connects directly
to the FlashFloppy device under test.

### Gotek Modifications

| STM32 Pin | Original Header Pin | New Header Pin
| --------- | ------------------- | --------------
| 26 (PB0)  | 18 (DIR)            | 28 (WRPROT)
| 15 (PA1)  | 20 (STEP)           | 2 (DSKCHG)
| 14 (PA0)  | 10 (SEL0)           | 8 (INDEX)
| 62 (PB9)  | 24 (WGATE)          | 26 (TRK0)
| 56 (PB4)  | 32 (SIDE)           | 34 (READY)
| 59 (PB7)  | 2 (DSKCHG)          | 18 (DIR)
| 61 (PB8)  | 8 (INDEX)           | 20 (STEP)
| 58 (PB6)  | 26 (TRK0)           | 10 (SEL0)
| 57 (PB5)  | 28 (WRPROT)         | 24 (WGATE)
| 55 (PB3)  | 34 (READY)          | 32 (SIDE)
| 23 (PA7)  | 30 (RDATA)          | 22 (WDATA)
| 41 (PA8)  | 22 (WDATA)          | 30 (RDATA)
| 16 (PA2)  | JUMPER_JB           | 12 (SEL1)
| 27 (PB1)  | JUMPER_JC           | 16 (MOTOR)

[Downloads]: https://github.com/keirf/FlashFloppy/wiki/Downloads
