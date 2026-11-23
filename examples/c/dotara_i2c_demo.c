/* File: dotara_i2c_demo.c
 *
 * Port of the Arduino sketch dotara_i2c_demo.ino to the dotara_wb_i2c master.
 * Three I2C slaves share one bus:
 *
 *   0x27  PCF8574 backpack with an HD44780 16x2 LCD
 *   0x48  ADS1115 16-bit ADC
 *   0x58  EEPROM (see EEPROM_ADDR below)
 *
 *   GPIOA[2] = SDA  (Tang Nano 9K pin 27)
 *   GPIOA[3] = SCL  (Tang Nano 9K pin 28)
 *   3.3 V supply, 4.7 kohm pull-up on each line
 *
 * Like the sketch, once per second the program
 *   1. reads back the float stored by the previous pass and prints it,
 *   2. reads ADS1115 channel A0 (+/-6.144 V range, 0.1875 mV per count),
 *   3. shows the voltage and the value read back in step 1 on the second
 *      LCD row, both in whole mV ("A0:1622 EE:1618"; the sketch shows only
 *      the voltage there),
 *   4. stores the voltage as a 4-byte float and advances the address by 4.
 *
 * On a 3.3 V supply a 16x2 panel is faint when both rows are driven. With
 * LCD_LINES = 1 only the top row is driven (1/8 duty instead of 1/16), which
 * gives full contrast. The two rows of the sketch then take turns on it:
 * the title for one pass, the reading for the next three. LCD_LINES = 2 shows
 * both rows as the sketch does, for a panel with a 5 V supply.
 *
 * The messages on UART0 are the ones the sketch prints on Serial. The baud
 * rate is 115200 (fixed by the SoC), not the 9600 of the sketch.
 */

#include <stdint.h>
#include "peripherals.h"

#define SYS_CLK_HZ        9000000UL
#define TICKS_PER_MS      (SYS_CLK_HZ / 1000UL)
#define TICKS_PER_US      (SYS_CLK_HZ / 1000000UL)
#define I2C_TIMEOUT_TICKS (25UL * TICKS_PER_MS)

#define FUNC_I2C          0x2u

#define I2C_PRESCALER     45u     /* SCL = clk / (3 * (prescaler + 1)) ~= 65 kHz */
#define I2C_CTR_EN        0x80u
#define I2C_CR_STA        0x80u
#define I2C_CR_STO        0x40u
#define I2C_CR_RD         0x20u
#define I2C_CR_WR         0x10u
#define I2C_CR_NACK       0x08u
#define I2C_CR_IACK       0x01u
#define I2C_SR_RXNACK     0x80u
#define I2C_SR_IF         0x01u

#define LCD_ADDR          0x27u
#define ADS_ADDR          0x48u

/* The sketch uses 0x58. On this EEPROM that address is not the 32 KB array
 * (which is at 0x50) but a separate 64-byte page: only the low 6 address bits
 * select a byte. ST M24256-D parts behave like this, and there a write to
 * this page with address bit 10 set is the command that locks the page
 * read-only for good. The sketch reaches bit 10 after 256 samples, so only
 * the low 6 bits of rom_addr are sent. Until then the chip sees the same
 * thing as with the sketch. */
#define EEPROM_ADDR       0x58u
#define EEPROM_ADDR_MASK  0x003Fu

/* ADS1115 */
#define ADS_REG_CONVERSION 0x00u
#define ADS_REG_CONFIG     0x01u
/* Start a single conversion, AIN0 to GND, +/-6.144 V, 128 SPS, comparator off:
 * what Adafruit_ADS1115::readADC_SingleEnded(0) uses with its default gain */
#define ADS_CONFIG_A0      0xC183u
#define ADS_CONFIG_READY   0x8000u
#define ADS_MV_PER_COUNT   0.1875f

/* PCF8574 -> HD44780 pin mapping */
#define LCD_RS            0x01u   /* P0: 0 = command, 1 = data */
#define LCD_RW            0x02u   /* P1: 0 = write, 1 = read */
#define LCD_EN            0x04u   /* P2: latch on falling edge */
#define LCD_BL            0x08u   /* P3: 1 = backlight on */
#define LCD_COLS          16u
#define LCD_LINES         2       /* rows driven: 1 (full contrast at 3.3 V) or 2 */

static int      lcd_present;
static uint8_t  lcd_screen[2][LCD_COLS];    /* the two rows of text */
static unsigned lcd_view;                   /* row on the glass when LCD_LINES = 1 */


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

    while ((timer_get() - start) < ((uint64_t)us * TICKS_PER_US))
        ;
}

static void delay_ms(uint32_t ms)
{
    delay_us(ms * 1000u);
}

/* Formats like Arduino's Print::print(float, digits). Returns the length. */
static unsigned fmt_float(char *dst, float number, unsigned digits)
{
    char     tmp[10];
    char    *p = dst;
    float    rounding = 0.5f;
    float    remainder;
    uint32_t int_part;
    unsigned i, n = 0;
    const char *special = 0;

    if (__builtin_isnan(number))
        special = "nan";
    else if (__builtin_isinf(number))
        special = "inf";
    else if (number > 4294967040.0f || number < -4294967040.0f)
        special = "ovf";

    if (special) {
        while (*special)
            *p++ = *special++;
        *p = '\0';
        return (unsigned)(p - dst);
    }

    if (number < 0.0f) {
        *p++ = '-';
        number = -number;
    }

    /* Round so that 1.999 with 2 digits becomes "2.00" */
    for (i = 0; i < digits; i++)
        rounding /= 10.0f;
    number += rounding;

    int_part  = (uint32_t)number;
    remainder = number - (float)int_part;

    do {
        tmp[n++] = (char)('0' + (int_part % 10u));
        int_part /= 10u;
    } while (int_part > 0);
    while (n--)
        *p++ = tmp[n];

    if (digits > 0)
        *p++ = '.';

    while (digits-- > 0) {
        unsigned digit;

        remainder *= 10.0f;
        digit = (unsigned)remainder;
        *p++ = (char)('0' + digit);
        remainder -= (float)digit;
    }

    *p = '\0';
    return (unsigned)(p - dst);
}

/* Writes an 8-character LCD field: a 3-character label and the value in
 * whole mV, cut or padded to 5 characters */
static void fmt_mv_field(char *dst, const char *label, float mv)
{
    char     num[20];
    unsigned i;
    unsigned n = fmt_float(num, mv, 0);

    for (i = 0; i < 3; i++)
        dst[i] = label[i];
    for (i = 0; i < 5; i++)
        dst[3 + i] = (i < n) ? num[i] : ' ';
}

/* Serial.println(value): two decimals */
static void uart_println_float(float value)
{
    char buf[20];

    fmt_float(buf, value, 2);
    uart_puts(buf);
    uart_puts("\r\n");
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
        if ((timer_get() - start) > I2C_TIMEOUT_TICKS)
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

static int i2c_probe(uint8_t addr)
{
    int ack = i2c_start() && i2c_write((uint8_t)(addr << 1));

    i2c_stop();
    return ack;
}


/* --------------------------------------------------------------------------
 * LCD (HD44780 in 4-bit mode behind a PCF8574)
 * -------------------------------------------------------------------------- */

static int pcf_write(uint8_t val)
{
    int ok = i2c_start()
          && i2c_write(LCD_ADDR << 1)
          && i2c_write(val);

    i2c_stop();
    return ok;
}

static int pcf_read(uint8_t *val)
{
    int ok = i2c_start()
          && i2c_write((LCD_ADDR << 1) | 1u)
          && i2c_read(val, 1);

    i2c_stop();
    return ok;
}

/* Clocks the upper nibble of nib into the display */
static int lcd_nibble(uint8_t nib, int is_data)
{
    uint8_t base = (uint8_t)((nib & 0xF0u) | LCD_BL | (is_data ? LCD_RS : 0u));
    int ok = pcf_write(base);

    delay_us(30);
    ok = pcf_write((uint8_t)(base | LCD_EN)) && ok;
    delay_us(50);
    ok = pcf_write(base) && ok;
    delay_us(600);

    return ok;
}

static int lcd_byte(int is_data, uint8_t val)
{
    int ok = lcd_nibble(val, is_data);

    ok = lcd_nibble((uint8_t)(val << 4), is_data) && ok;
    if (!is_data)
        delay_us(1500);

    return ok;
}

static int lcd_cmd(uint8_t cmd)
{
    int ok = lcd_byte(0, cmd);

    if (cmd == 0x01u || cmd == 0x02u)       /* clear and home are slow */
        delay_ms(15);

    return ok;
}

/* Reads the character at the cursor. D7..D4 are written high so that the
 * PCF8574 pins are inputs while the display drives them. */
static int lcd_read_char(uint8_t *val)
{
    uint8_t base = (uint8_t)(0xF0u | LCD_BL | LCD_RW | LCD_RS);
    uint8_t hi = 0, lo = 0;
    int ok = pcf_write(base);

    ok = pcf_write((uint8_t)(base | LCD_EN)) && ok;
    ok = pcf_read(&hi) && ok;
    ok = pcf_write(base) && ok;
    ok = pcf_write((uint8_t)(base | LCD_EN)) && ok;
    ok = pcf_read(&lo) && ok;
    ok = pcf_write(base) && ok;
    delay_us(100);

    *val = (uint8_t)((hi & 0xF0u) | (lo >> 4));
    return ok;
}

/* Brings the display into 4-bit mode from any state, including a lost nibble */
static int lcd_sync(void)
{
    int ok = pcf_write(LCD_BL);

    delay_ms(2);
    ok = lcd_nibble(0x30u, 0) && ok;
    delay_ms(10);
    ok = lcd_nibble(0x30u, 0) && ok;
    delay_ms(3);
    ok = lcd_nibble(0x30u, 0) && ok;
    delay_ms(3);
    ok = lcd_nibble(0x20u, 0) && ok;
    delay_ms(3);

    ok = lcd_cmd((LCD_LINES == 1) ? 0x20u : 0x28u) && ok;   /* 4-bit, 5x8 font */
    ok = lcd_cmd(0x06u) && ok;              /* cursor moves right */
    ok = lcd_cmd(0x0Cu) && ok;              /* display on, no cursor */

    return ok;
}

/* lcd.init() and lcd.backlight() */
static void lcd_init(void)
{
    unsigned row, col;

    for (row = 0; row < 2; row++)
        for (col = 0; col < LCD_COLS; col++)
            lcd_screen[row][col] = ' ';

    lcd_present = i2c_probe(LCD_ADDR);
    if (!lcd_present)
        return;

    delay_ms(60);
    lcd_sync();
    lcd_cmd(0x01u);
}

/* Writes lcd_screen[row][col .. col+len-1] and reads it back from the display.
 * With one row driven, a row that is not on the glass has nothing to write. */
static int lcd_write_span(unsigned row, unsigned col, unsigned len)
{
    uint8_t  pos = (uint8_t)(0x80u | col);
    unsigned i;
    int ok;

    if (LCD_LINES == 1) {
        if (row != lcd_view)
            return 1;
    } else if (row == 1) {
        pos |= 0x40u;
    }

    ok = lcd_cmd(pos);
    for (i = 0; i < len; i++)
        ok = lcd_byte(1, lcd_screen[row][col + i]) && ok;

    ok = lcd_cmd(pos) && ok;
    for (i = 0; ok && i < len; i++) {
        uint8_t got = 0;

        ok = lcd_read_char(&got) && got == lcd_screen[row][col + i];
    }

    return ok;
}

/* Draws part of a row. If the read-back differs, the display has lost a
 * nibble: it is synchronized again and everything is redrawn. */
static void lcd_draw(unsigned row, unsigned col, unsigned len)
{
    unsigned attempt;

    if (lcd_write_span(row, col, len))
        return;

    for (attempt = 0; attempt < 3; attempt++) {
        uart_puts("LCD read-back mismatch, synchronizing again\r\n");
        lcd_sync();
        if (lcd_write_span(0, 0, LCD_COLS) && lcd_write_span(1, 0, LCD_COLS))
            return;
    }
}

/* lcd.setCursor(col, row) followed by lcd.print(s). Text past column 15 is
 * dropped, on the real display it is not visible either. */
static void lcd_print_at(unsigned col, unsigned row, const char *s)
{
    unsigned len = 0;

    if (!lcd_present)
        return;

    while (s[len] != '\0' && (col + len) < LCD_COLS) {
        lcd_screen[row][col + len] = (uint8_t)s[len];
        len++;
    }

    lcd_draw(row, col, len);
}

/* With one row driven: chooses which of the two rows of text is on the glass */
static void lcd_view_row(unsigned row)
{
    if (!lcd_present || LCD_LINES != 1 || row == lcd_view)
        return;

    lcd_view = row;
    lcd_draw(row, 0, LCD_COLS);
}


/* --------------------------------------------------------------------------
 * ADS1115
 * -------------------------------------------------------------------------- */

static int ads_write_reg(uint8_t reg, uint16_t val)
{
    int ok = i2c_start()
          && i2c_write(ADS_ADDR << 1)
          && i2c_write(reg)
          && i2c_write((uint8_t)(val >> 8))
          && i2c_write((uint8_t)val);

    i2c_stop();
    return ok;
}

static int ads_read_reg(uint8_t reg, uint16_t *val)
{
    uint8_t hi = 0, lo = 0;
    int ok = i2c_start()
          && i2c_write(ADS_ADDR << 1)
          && i2c_write(reg);

    i2c_stop();
    if (!ok)
        return 0;

    ok = i2c_start()
      && i2c_write((ADS_ADDR << 1) | 1u)
      && i2c_read(&hi, 0)
      && i2c_read(&lo, 1);
    i2c_stop();

    *val = (uint16_t)(((uint16_t)hi << 8) | lo);
    return ok;
}

/* ads.readADC_SingleEnded(0): a signed result, as in the Adafruit library */
static int16_t ads_read_a0(void)
{
    uint16_t status = 0, conv = 0;
    unsigned tries;

    ads_write_reg(ADS_REG_CONFIG, ADS_CONFIG_A0);

    for (tries = 0; tries < 25; tries++) {
        delay_ms(1);
        if (ads_read_reg(ADS_REG_CONFIG, &status) && (status & ADS_CONFIG_READY))
            break;
    }

    ads_read_reg(ADS_REG_CONVERSION, &conv);
    return (int16_t)conv;
}


/* --------------------------------------------------------------------------
 * EEPROM
 * -------------------------------------------------------------------------- */

typedef union {
    float   value;
    uint8_t bytes[4];
} float_bytes_t;

static int eeprom_set_pointer(uint16_t rom_addr)
{
    rom_addr &= EEPROM_ADDR_MASK;

    return i2c_start()
        && i2c_write(EEPROM_ADDR << 1)
        && i2c_write((uint8_t)(rom_addr >> 8))
        && i2c_write((uint8_t)rom_addr);
}

static void write_float_to_eeprom(uint16_t rom_addr, float value)
{
    float_bytes_t f;
    unsigned i;
    int ok = eeprom_set_pointer(rom_addr);

    f.value = value;
    for (i = 0; ok && i < 4; i++)
        ok = i2c_write(f.bytes[i]);

    i2c_stop();
    delay_ms(5);                            /* EEPROM internal write cycle */
}

static float read_float_from_eeprom(uint16_t rom_addr)
{
    float_bytes_t f;
    unsigned i;
    int ok = eeprom_set_pointer(rom_addr);

    f.value = 0.0f;
    i2c_stop();
    if (!ok)
        return f.value;

    ok = i2c_start() && i2c_write((EEPROM_ADDR << 1) | 1u);
    for (i = 0; ok && i < 4; i++)
        ok = i2c_read(&f.bytes[i], i == 3);
    i2c_stop();

    return f.value;
}


/* --------------------------------------------------------------------------
 * Demo
 * -------------------------------------------------------------------------- */

int main(void)
{
    uint16_t rom_addr = 0;

    /* GPIOA[3:2] -> I2C; FUNC0 covers GPIOA[3:0], leave pins 1:0 alone */
    *GPIOA_FUNC0_ADDRESS = (*GPIOA_FUNC0_ADDRESS & ~0xFF00u)
                         | (FUNC_I2C << 8)
                         | (FUNC_I2C << 12);

    *I2C_PRER_LO_ADDRESS = I2C_PRESCALER & 0xFFu;
    *I2C_PRER_HI_ADDRESS = (I2C_PRESCALER >> 8) & 0xFFu;
    *I2C_CTR_ADDRESS     = I2C_CTR_EN;
    delay_us(50);

    lcd_init();

    if (!i2c_probe(ADS_ADDR)) {
        uart_puts("Failed to initialize ADS1115!\r\n");
        while (1)
            ;
    }

    lcd_print_at(1, 0, "DOTARA I2C TEST");
    lcd_print_at(0, 1, "A0:     EE:     ");

    while (1) {
        char     text[32];
        float    read_from_rom;
        float    voltage;
        int16_t  adc0;

        read_from_rom = read_float_from_eeprom(rom_addr == 0 ? rom_addr : rom_addr - 4);

        uart_puts("Read from EEPROM: ");
        uart_println_float(read_from_rom);

        /* The sketch keeps this in a uint16_t. A grounded input reads a few
         * counts below zero, which the sketch then shows as about 12280 mV. */
        adc0    = ads_read_a0();
        voltage = (float)adc0 * ADS_MV_PER_COUNT;

        fmt_float(text, voltage, 2);
        uart_puts("A0: ");
        uart_puts(text);
        uart_puts(" mV\r\n");

        /* Current reading next to the one read back from the EEPROM */
        fmt_mv_field(&text[0], "A0:", voltage);
        fmt_mv_field(&text[8], "EE:", read_from_rom);
        text[LCD_COLS] = '\0';
        lcd_view_row(((rom_addr / 4u) & 3u) == 0u ? 0u : 1u);
        lcd_print_at(0, 1, text);

        write_float_to_eeprom(rom_addr, voltage);
        uart_puts("Written into EEPROM: ");
        uart_println_float(voltage);

        rom_addr += 4;

        delay_ms(1000);
    }
}
