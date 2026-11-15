/* File: i2c_demo.c
 *
 * I2C master demonstration (GPIOA[3:2]): bus scan and an EEPROM read/write
 * test.
 *
 *   GPIOA[2] = SDA  (Tang Nano 9K pin 27)
 *   GPIOA[3] = SCL  (Tang Nano 9K pin 28)
 *
 * Wiring: connect SDA and SCL to the device and add a 4.7 kohm pull-up
 * resistor from each line to 3.3 V.
 *
 * 1. Scans addresses 0x08 - 0x77 and prints every device that acknowledges.
 * 2. If a device answers at 0x50 it is treated as a 24Cxx EEPROM: four bytes
 *    are written (addresses 0x10, 0x11, 0x20, 0x21) and read back.
 *
 * The master has no repeated START, so every read is done as "write the
 * address pointer, STOP, START, read one byte with NACK, STOP".
 *
 * The result is printed on UART0 (USB serial, 115200 baud) and shown on the
 * LEDs (active-low): LED0 on = pass, LED5 on = fail.
 */

#include <stdint.h>
#include "peripherals.h"

#define SYS_CLK_HZ      9000000UL
#define TICKS_PER_MS    (SYS_CLK_HZ / 1000UL)
#define TICKS_PER_US    (SYS_CLK_HZ / 1000000UL)
#define TIMEOUT_TICKS   (20UL * TICKS_PER_MS)

#define FUNC_I2C        0x2u

#define I2C_PRESCALER   45u         /* SCL = clk / (3 * (prescaler + 1)) */
#define I2C_CTR_EN      0x80u

#define I2C_CR_STA      0x80u
#define I2C_CR_STO      0x40u
#define I2C_CR_RD       0x20u
#define I2C_CR_WR       0x10u
#define I2C_CR_NACK     0x08u       /* answer a received byte with NACK */
#define I2C_CR_IACK     0x01u       /* clear the interrupt flag */

#define I2C_SR_RXNACK   0x80u       /* 1 = the slave did not acknowledge */
#define I2C_SR_IF       0x01u       /* command finished */

#define PAD_SDA         (1u << 2)
#define PAD_SCL         (1u << 3)

#define EEPROM_ADDR     0x50u
#define EEPROM_ADDR_BYTES 2u        /* 1 for 24C01-24C16, 2 for 24C32 and larger (e.g. AT24C256) */

static int failures;


/* --------------------------------------------------------------------------
 * Helpers
 * -------------------------------------------------------------------------- */

static void uart_putc(char c)
{
    while (!(*UART_TX_STATUS_ADDRESS & (1u << UART_TX_STATUS_IDX_EMPTY)))
        ;
    *UART_BUFFER_ADDRESS = (uint8_t)c;
}

static void uart_puts(const char *s)
{
    while (*s)
        uart_putc(*s++);
}

static void uart_put_hex(uint32_t v, int digits)
{
    while (digits--)
        uart_putc("0123456789ABCDEF"[(v >> (digits * 4)) & 0xFu]);
}

static uint64_t timer_get(void)
{
    uint32_t hi1, lo, hi2;

    do {
        hi1 = *TIMER_MTIMEH_ADDRESS;
        lo  = *TIMER_MTIME_ADDRESS;
        hi2 = *TIMER_MTIMEH_ADDRESS;
    } while (hi1 != hi2);

    return ((uint64_t)hi2 << 32) | lo;
}

static void delay_us(uint32_t us)
{
    uint64_t start = timer_get();

    while ((timer_get() - start) < (us * TICKS_PER_US))
        ;
}

static void delay_ms(uint32_t ms)
{
    delay_us(ms * 1000u);
}

static int timed_out(uint64_t start)
{
    return (timer_get() - start) > TIMEOUT_TICKS;
}

static void report(const char *name, int ok)
{
    uart_puts(name);
    uart_puts(ok ? ": PASS\r\n" : ": FAIL\r\n");

    if (!ok)
        failures++;
}


/* --------------------------------------------------------------------------
 * I2C
 * -------------------------------------------------------------------------- */

/* Issue one command and wait until it is finished. Returns 0 on timeout. */
static int i2c_cmd(uint32_t cmd)
{
    uint64_t start;

    *I2C_CR_SR_ADDRESS = I2C_CR_IACK;
    *I2C_CR_SR_ADDRESS = cmd;

    start = timer_get();
    while (!(*I2C_CR_SR_ADDRESS & I2C_SR_IF)) {
        if (timed_out(start))
            return 0;
    }

    return 1;
}

static int i2c_start(void)
{
    return i2c_cmd(I2C_CR_STA);
}

static int i2c_stop(void)
{
    return i2c_cmd(I2C_CR_STO);
}

/* Returns 1 if the slave acknowledged the byte, 0 on NACK or timeout */
static int i2c_write(uint8_t byte)
{
    *I2C_RXR_TXR_ADDRESS = byte;

    if (!i2c_cmd(I2C_CR_WR))
        return 0;

    return !(*I2C_CR_SR_ADDRESS & I2C_SR_RXNACK);
}

/* Reads one byte; last = 1 ends the transfer with a NACK */
static int i2c_read(uint8_t *byte, int last)
{
    if (!i2c_cmd(I2C_CR_RD | (last ? I2C_CR_NACK : 0)))
        return 0;

    *byte = (uint8_t)*I2C_RXR_TXR_ADDRESS;
    return 1;
}

/* Sends the memory address: high byte first on 2-byte-addressed parts */
static int eeprom_set_pointer(uint8_t mem)
{
    if (EEPROM_ADDR_BYTES == 2u && !i2c_write(0x00))
        return 0;

    return i2c_write(mem);
}

static int eeprom_write(uint8_t mem, uint8_t data)
{
    int ok = i2c_start()
          && i2c_write(EEPROM_ADDR << 1)
          && eeprom_set_pointer(mem)
          && i2c_write(data);

    i2c_stop();
    delay_ms(6);                    /* EEPROM internal write cycle */

    return ok;
}

static int eeprom_read(uint8_t mem, uint8_t *data)
{
    int ok;

    /* Dummy write: sets the EEPROM address pointer */
    ok = i2c_start()
      && i2c_write(EEPROM_ADDR << 1)
      && eeprom_set_pointer(mem);
    i2c_stop();

    if (!ok)
        return 0;

    ok = i2c_start()
      && i2c_write((EEPROM_ADDR << 1) | 1)
      && i2c_read(data, 1);
    i2c_stop();

    return ok;
}


/* --------------------------------------------------------------------------
 * Demo
 * -------------------------------------------------------------------------- */

static const uint8_t ee_addr[] = { 0x10, 0x11, 0x20, 0x21 };
static const uint8_t ee_data[] = { 0x5A, 0xC3, 0x00, 0x3C };

int main(void)
{
    unsigned addr;
    unsigned found = 0;
    unsigned i;
    int      eeprom_found = 0;

    failures = 0;

    uart_puts("\r\nI2C DEMO\r\n");
    uart_puts("I2C: SDA = GPIOA[2] (pin 27), SCL = GPIOA[3] (pin 28)\r\n");

    /* GPIOA[3:2] -> I2C; FUNC0 covers GPIOA[3:0], leave pins 1:0 alone */
    *GPIOA_FUNC0_ADDRESS = (*GPIOA_FUNC0_ADDRESS & ~0xFF00u)
                         | (FUNC_I2C << 8)
                         | (FUNC_I2C << 12);

    *I2C_PRER_LO_ADDRESS = I2C_PRESCALER & 0xFFu;
    *I2C_PRER_HI_ADDRESS = (I2C_PRESCALER >> 8) & 0xFFu;
    *I2C_CTR_ADDRESS     = I2C_CTR_EN;
    delay_us(10);

    report("I2C lines idle high (pull-ups present)",
           (*GPIOA_DATA_ADDRESS & (PAD_SDA | PAD_SCL)) == (PAD_SDA | PAD_SCL));

    uart_puts("Scanning 0x08 - 0x77\r\n");
    for (addr = 0x08; addr <= 0x77; addr++) {
        int ack = i2c_start() && i2c_write((uint8_t)(addr << 1));

        i2c_stop();

        if (ack) {
            found++;
            uart_puts("  found 0x");
            uart_put_hex(addr, 2);
            uart_puts("\r\n");

            if (addr == EEPROM_ADDR)
                eeprom_found = 1;
        }
    }

    uart_puts("Devices found: 0x");
    uart_put_hex(found, 2);
    uart_puts("\r\n");

    if (eeprom_found) {
        unsigned good = 0;

        uart_puts("EEPROM at 0x50: write and read back\r\n");

        for (i = 0; i < sizeof(ee_addr); i++)
            eeprom_write(ee_addr[i], ee_data[i]);

        for (i = 0; i < sizeof(ee_addr); i++) {
            uint8_t v = 0;
            int ok = eeprom_read(ee_addr[i], &v);

            if (ok && v == ee_data[i]) {
                good++;
            } else {
                uart_puts("  [0x");
                uart_put_hex(ee_addr[i], 2);
                uart_puts("] wrote 0x");
                uart_put_hex(ee_data[i], 2);
                uart_puts(ok ? ", read 0x" : ", no reply, last 0x");
                uart_put_hex(v, 2);
                uart_puts("\r\n");
            }
        }

        report("EEPROM write/read", good == sizeof(ee_addr));
    } else {
        uart_puts("No device at 0x50, EEPROM test skipped\r\n");
    }

    if (failures) {
        uart_puts("RESULT: FAIL\r\n");
        *LEDS_ADDRESS = (uint16_t)~0x20u;
    } else {
        uart_puts("RESULT: PASS\r\n");
        *LEDS_ADDRESS = (uint16_t)~0x01u;
    }

    uart_puts("DEMO DONE\r\n");

    while (1)
        ;
}
