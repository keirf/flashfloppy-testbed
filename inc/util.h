/*
 * util.h
 * 
 * Utility definitions.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#define ASSERT(p) do { if (!(p)) illegal(); } while (0)

#define BUG_ON(p) do { if ((p)) __bug(#p, __FILE__, __LINE__); } while (0)
void __attribute__((noreturn)) __bug(const char *p, const char *file,
                                     unsigned int line);

typedef char bool_t;
#define TRUE 1
#define FALSE 0

#define LONG_MAX ((long int)((~0UL)>>1))
#define LONG_MIN ((long int)~LONG_MAX)

#ifndef offsetof
#define offsetof(a,b) __builtin_offsetof(a,b)
#endif
#define container_of(ptr, type, member) ({                      \
        typeof( ((type *)0)->member ) *__mptr = (ptr);          \
        (type *)( (char *)__mptr - offsetof(type,member) );})
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

#define min(x,y) ({                             \
    const typeof(x) _x = (x);                   \
    const typeof(y) _y = (y);                   \
    (void) (&_x == &_y);                        \
    _x < _y ? _x : _y; })

#define max(x,y) ({                             \
    const typeof(x) _x = (x);                   \
    const typeof(y) _y = (y);                   \
    (void) (&_x == &_y);                        \
    _x > _y ? _x : _y; })

#define min_t(type,x,y) \
    ({ type __x = (x); type __y = (y); __x < __y ? __x: __y; })
#define max_t(type,x,y) \
    ({ type __x = (x); type __y = (y); __x > __y ? __x: __y; })

/* Fast memset/memcpy: Pointers must be word-aligned, count must be a non-zero 
 * multiple of 32 bytes. */
void memset_fast(void *s, int c, size_t n);
void memcpy_fast(void *dest, const void *src, size_t n);

void *memset(void *s, int c, size_t n);
void *memcpy(void *dest, const void *src, size_t n);
void *memmove(void *dest, const void *src, size_t n);
int memcmp(const void *s1, const void *s2, size_t n);

size_t strlen(const char *s);
size_t strnlen(const char *s, size_t maxlen);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
char *strcpy(char *dest, const char *src);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
int tolower(int c);
int toupper(int c);
int isspace(int c);

long int strtol(const char *nptr, char **endptr, int base);

void qsort_p(void *base, unsigned int nr,
             int (*compar)(const void *, const void *));

int vsnprintf(char *str, size_t size, const char *format, va_list ap)
    __attribute__ ((format (printf, 3, 0)));

int snprintf(char *str, size_t size, const char *format, ...)
    __attribute__ ((format (printf, 3, 4)));

#define le16toh(x) (x)
#define le32toh(x) (x)
#define htole16(x) (x)
#define htole32(x) (x)
#define be16toh(x) _rev16(x)
#define be32toh(x) _rev32(x)
#define htobe16(x) _rev16(x)
#define htobe32(x) _rev32(x)

uint32_t rand(void);

/* Board-specific callouts */
void board_init(void);

/* Serial console control */
void console_init(void);
void console_sync(void);
void console_barrier(void);
void console_crash_on_input(void);

/* Serial console output */
int vprintk(const char *format, va_list ap)
    __attribute__ ((format (printf, 1, 0)));
int printk(const char *format, ...)
    __attribute__ ((format (printf, 1, 2)));

/* CRC-CCITT */
uint16_t crc16_ccitt(const void *buf, size_t len, uint16_t crc);

/* Display: 3-digit 7-segment display */
void led_7seg_init(void);
void led_7seg_write_string(const char *p);
void led_7seg_write_decimal(unsigned int val);
void led_7seg_display_setting(bool_t enable);

/* Text/data/BSS address ranges. */
extern char _stext[], _etext[];
extern char _smaintext[], _emaintext[];
extern char _sdat[], _edat[], _ldat[];
extern char _sbss[], _ebss[];

/* Stacks. */
extern uint32_t _thread_stacktop[], _thread_stackbottom[];
extern uint32_t _irq_stacktop[], _irq_stackbottom[];

/* Default exception handler. */
void EXC_unused(void);

/* IRQ priorities, 0 (highest) to 15 (lowest). */
#define RESET_IRQ_PRI         0
#define FLOPPY_IRQ_INDEX_PRI  1
#define TIMER_IRQ_PRI         4
#define FLOPPY_IRQ_DSKCHG_PRI 5
#define CONSOLE_IRQ_PRI      15

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
