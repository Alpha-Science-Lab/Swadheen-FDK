/* File: soccer_bot.c
 *
 * Soccer robot with two BTS7960 motor drivers, controlled over Bluetooth
 * (HC-05 on UART1) or the USB serial port (UART0).
 *
 *   Tang Nano 9K pin 29  GPIOA[4]  PWM ch0  left  motor RPWM (forward)
 *   Tang Nano 9K pin 30  GPIOA[5]  PWM ch1  left  motor LPWM (reverse)
 *   Tang Nano 9K pin 33  GPIOA[6]  PWM ch2  right motor RPWM (forward)
 *   Tang Nano 9K pin 34  GPIOA[7]  PWM ch3  right motor LPWM (reverse)
 *   Tang Nano 9K pin 25  GPIOA[0]  UART1 TX -> HC-05 RXD
 *   Tang Nano 9K pin 26  GPIOA[1]  UART1 RX <- HC-05 TXD
 *
 * UART1 runs at 115200 baud (set in tang9k_top.sv); the Bluetooth module must match. R_EN and L_EN of both BTS7960
 * modules must be tied high.
 *
 * Commands (same as rc_car, 115200 baud):
 *   F/f forward   B/b backward   L/l spin left   R/r spin right
 *   H/h/S/s halt (brake)   + faster   - slower   ? status
 *   .  keepalive (no reply)
 *
 * A motion started over Bluetooth halts after DEADMAN_MS without any byte from
 * UART1, so the controller must repeat a command or send '.' while driving.
 * Motion started over USB is never timed out.
 *
 * Speed is the PWM duty in percent: 50 at start, 20 to 100 in steps of 10.
 * Replies are sent on both UARTs.
 */

#include <stdint.h>
#include "peripherals.h"

#define PWM_PRESCALER   89      /* 9 MHz / (89 + 1) = 100 kHz tick */
#define PWM_PERIOD      99      /* 99 + 1 = 100 ticks = 1 kHz */

#define SPEED_INITIAL   50u
#define SPEED_MIN       20u
#define SPEED_MAX       100u
#define SPEED_STEP      10u

/* PWM channel on each BTS7960 input; swap FWD and REV if a motor runs backwards */
#define LEFT_FWD        0u
#define LEFT_REV        1u
#define RIGHT_FWD       2u
#define RIGHT_REV       3u

#define FUNC_UART       0x1u
#define FUNC_PWM        0x5u

#define SAMPLES         30000u

#define TICKS_PER_MS    9000u   /* 9 MHz timer */
#define DEADMAN_MS      600u

#define TX_RING_SIZE    512u    /* power of two */

static uint32_t speed = SPEED_INITIAL;
static int left_dir;            /* +1 forward, -1 reverse, 0 brake */
static int right_dir;

/* Startup code does not clear .bss; main() initialises all of these */
static uint8_t  tx_ring[TX_RING_SIZE];
static uint32_t tx_head;
static uint32_t tx_tail;
static int      cmd_from_uart1; /* source of the latest command byte */
static int      bt_driving;     /* latest command came over UART1 */
static uint32_t last_rx_time;


/* --------------------------------------------------------------------------
 * UART
 * -------------------------------------------------------------------------- */

/* Queued so the main loop keeps polling the 1-byte receivers while a reply is sent */
static void put_char(char c)
{
    uint32_t next = (tx_head + 1u) & (TX_RING_SIZE - 1u);

    if (next == tx_tail)
        return;

    tx_ring[tx_head] = (uint8_t)c;
    tx_head = next;
}

static void tx_pump(void)
{
    if (tx_head == tx_tail)
        return;

    if (!(*UART_TX_STATUS_ADDRESS & (1u << UART_TX_STATUS_IDX_EMPTY)) ||
        !(*UART1_TX_STATUS_ADDRESS & (1u << UART_TX_STATUS_IDX_EMPTY)))
        return;

    uint8_t c = tx_ring[tx_tail];

    tx_tail = (tx_tail + 1u) & (TX_RING_SIZE - 1u);
    *UART_BUFFER_ADDRESS  = c;
    *UART1_BUFFER_ADDRESS = c;
}

static void put_str(const char *s)
{
    while (*s)
        put_char(*s++);
}

static void put_dec(uint32_t v)
{
    char hundreds = '0';
    char tens     = '0';

    while (v >= 100u) {
        v -= 100u;
        hundreds++;
    }

    while (v >= 10u) {
        v -= 10u;
        tens++;
    }

    if (hundreds != '0')
        put_char(hundreds);

    if (hundreds != '0' || tens != '0')
        put_char(tens);

    put_char((char)('0' + v));
}

/* Next command byte from either UART, or -1 when none is waiting */
static int get_command(void)
{
    if (*UART_RX_STATUS_ADDRESS & (1u << UART_RX_STATUS_IDX_FULL)) {
        cmd_from_uart1 = 0;
        return *UART_BUFFER_ADDRESS;
    }

    if (*UART1_RX_STATUS_ADDRESS & (1u << UART_RX_STATUS_IDX_FULL)) {
        cmd_from_uart1 = 1;
        return *UART1_BUFFER_ADDRESS;
    }

    return -1;
}


/* --------------------------------------------------------------------------
 * Motors
 * -------------------------------------------------------------------------- */

static void set_motor(uint32_t fwd, uint32_t rev, int dir)
{
    volatile uint32_t *duty = PWM_DUTY_BASE_ADDRESS;

    /* The channel that turns off is written first so both are never on together */
    if (dir > 0) {
        duty[rev] = 0;
        duty[fwd] = speed;
    } else {
        duty[fwd] = 0;
        duty[rev] = (dir < 0) ? speed : 0;
    }
}

static void apply(void)
{
    set_motor(LEFT_FWD, LEFT_REV, left_dir);
    set_motor(RIGHT_FWD, RIGHT_REV, right_dir);
}

static void halt(void)
{
    left_dir  = 0;
    right_dir = 0;
    apply();
}

static void check_deadman(void)
{
    if (!bt_driving || (left_dir == 0 && right_dir == 0))
        return;

    if ((*TIMER_MTIME_ADDRESS - last_rx_time) > (DEADMAN_MS * TICKS_PER_MS)) {
        halt();
        put_str("TIMEOUT\r\n");
    }
}

static void drive(int left, int right, const char *name)
{
    left_dir  = left;
    right_dir = right;
    apply();

    put_str(name);
    put_char(' ');
    put_dec(speed);
    put_str("%\r\n");
}

static void change_speed(int up)
{
    if (up && speed < SPEED_MAX)
        speed += SPEED_STEP;
    else if (!up && speed > SPEED_MIN)
        speed -= SPEED_STEP;

    apply();

    put_str("SPEED ");
    put_dec(speed);
    put_str("%\r\n");
}


/* --------------------------------------------------------------------------
 * Status
 * -------------------------------------------------------------------------- */

static void put_four(const char *label, const uint32_t v[4])
{
    static const char *const pins[4] = { " pin29=", " pin30=", " pin33=", " pin34=" };

    put_str(label);

    for (int k = 0; k < 4; k++) {
        put_str(pins[k]);
        put_dec(v[k]);
    }

    put_str("\r\n");
}

/* Percentage of time each PWM pad is high, read back from GPIOA[7:4] */
static void measure_pads(uint32_t pct[4])
{
    uint32_t n[4] = { 0, 0, 0, 0 };

    for (uint32_t i = 0; i < SAMPLES; i++) {
        uint32_t d = *GPIOA_DATA_ADDRESS >> 4;

        n[0] += d & 1u;
        n[1] += (d >> 1) & 1u;
        n[2] += (d >> 2) & 1u;
        n[3] += (d >> 3) & 1u;
    }

    for (int k = 0; k < 4; k++) {
        uint32_t v = n[k] + SAMPLES / 200u;
        uint32_t p = 0;

        while (v >= SAMPLES / 100u) {
            v -= SAMPLES / 100u;
            p++;
        }

        pct[k] = p;
    }
}

static void show_status(void)
{
    uint32_t duty[4];
    uint32_t pads[4];

    for (uint32_t k = 0; k < 4; k++)
        duty[k] = *(PWM_DUTY_BASE_ADDRESS + k);

    measure_pads(pads);

    put_str("STATUS speed=");
    put_dec(speed);
    put_str("%\r\n");
    put_four("  duty set  ", duty);
    put_four("  pads high%", pads);
}


/* --------------------------------------------------------------------------
 * Setup and main loop
 * -------------------------------------------------------------------------- */

static void init_hardware(void)
{
    *PWM_PRESCALER_ADDRESS = PWM_PRESCALER;
    *PWM_PERIOD_ADDRESS    = PWM_PERIOD;

    for (uint32_t k = 0; k < 4; k++)
        *(PWM_DUTY_BASE_ADDRESS + k) = 0;

    *PWM_ENABLE_ADDRESS = 0xFu;
    *PWM_INVERT_ADDRESS = 0;
    *PWM_CTRL_ADDRESS   = 1;

    /* FUNC0 covers GPIOA[3:0]: pins 1:0 = UART1, pins 3:2 left alone */
    *GPIOA_FUNC0_ADDRESS = (*GPIOA_FUNC0_ADDRESS & ~0x00FFu)
                         | (FUNC_UART << 0)
                         | (FUNC_UART << 4);

    /* FUNC1 covers GPIOA[7:4] = PWM ch0..3 */
    *GPIOA_FUNC1_ADDRESS = (*GPIOA_FUNC1_ADDRESS & ~0xFFFFu)
                         | (FUNC_PWM << 0)
                         | (FUNC_PWM << 4)
                         | (FUNC_PWM << 8)
                         | (FUNC_PWM << 12);
}

int main(void)
{
    /* Startup code does not clear .bss */
    left_dir  = 0;
    right_dir = 0;
    tx_head   = 0;
    tx_tail   = 0;
    cmd_from_uart1 = 0;
    bt_driving     = 0;
    last_rx_time   = 0;

    init_hardware();

    /* Onboard LEDs are active-low: LED0 on = ready */
    *LEDS_ADDRESS = (uint16_t)~0x01u;

    put_str("\r\nSOCCER BOT READY\r\n");
    put_str("F B L R H + - ?  speed ");
    put_dec(speed);
    put_str("%\r\n");

    while (1) {
        int c;

        tx_pump();
        check_deadman();

        c = get_command();

        if (c != -1 && c != '\r' && c != '\n' && c != ' ') {
            bt_driving   = cmd_from_uart1;
            last_rx_time = *TIMER_MTIME_ADDRESS;
        }

        switch (c) {
        case -1:
        case '\r':
        case '\n':
        case ' ':
        case '.':
            break;
        case 'F':
        case 'f':
            drive(1, 1, "FORWARD");
            break;
        case 'B':
        case 'b':
            drive(-1, -1, "BACKWARD");
            break;
        case 'L':
        case 'l':
            drive(-1, 1, "LEFT");
            break;
        case 'R':
        case 'r':
            drive(1, -1, "RIGHT");
            break;
        case 'H':
        case 'h':
        case 'S':
        case 's':
            halt();
            put_str("HALT\r\n");
            break;
        case '+':
            change_speed(1);
            break;
        case '-':
            change_speed(0);
            break;
        case '?':
            show_status();
            break;
        default:
            put_str("UNKNOWN '");
            put_char((char)c);
            put_str("'\r\n");
            break;
        }
    }
}
