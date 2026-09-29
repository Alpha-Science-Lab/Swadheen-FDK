/* File: rc_car.c
 *
 * Bluetooth-controlled RC Car firmware
 *
 * Hardware:
 *   GPIOA[0] Motor A IN1
 *   GPIOA[1] Motor A IN2
 *   GPIOA[2] Motor B IN1
 *   GPIOA[3] Motor B IN2
 *   GPIOA[4] PWM Channel 0 -> Motor A speed
 *   GPIOA[5] PWM Channel 1 -> Motor B speed
 *
 *   UART communication \
 *      with HC-05 Bluetooth module
 *
 * Commands:
 *   F / f : Forward
 *   B / b : Backward
 *   L / l : Turn Left
 *   R / r : Turn Right
 *   H / h : Halt
 *   +     : Increase speed
 *   -     : Decrease speed
 *
 * PWM:
 *   System clock = 9 MHz
 *   Prescaler    = 89
 *   PWM tick     = 100 kHz
 *   Period       = 100
 *   PWM frequency = 1 kHz
 *
 * Speed:
 *   Initial = 50%
 *   Minimum = 20%
 *   Maximum = 100%
 *   Step    = 10%
 */

#include <stdint.h>
#include "peripherals.h"


/* --------------------------------------------------------------------------
 * System / PWM configuration
 * -------------------------------------------------------------------------- */

#define SYS_CLK_HZ          9000000UL

#define PWM_PRESCALER       89
#define PWM_PERIOD          100

#define SPEED_INITIAL       50
#define SPEED_MIN           20
#define SPEED_MAX           100
#define SPEED_STEP          10


/* --------------------------------------------------------------------------
 * GPIO direction definitions
 *
 * Motor A:
 *   GPIOA[0] = IN1
 *   GPIOA[1] = IN2
 *
 * Motor B:
 *   GPIOA[2] = IN1
 *   GPIOA[3] = IN2
 * -------------------------------------------------------------------------- */

#define MOTOR_A_IN1         (1u << 0)
#define MOTOR_A_IN2         (1u << 1)

#define MOTOR_B_IN1         (1u << 2)
#define MOTOR_B_IN2         (1u << 3)

#define MOTOR_A_MASK        (MOTOR_A_IN1 | MOTOR_A_IN2)
#define MOTOR_B_MASK        (MOTOR_B_IN1 | MOTOR_B_IN2)

#define MOTOR_GPIO_MASK     (MOTOR_A_MASK | MOTOR_B_MASK)


/* --------------------------------------------------------------------------
 * UART functions
 * -------------------------------------------------------------------------- */

static inline void uart_write_char(char c)
{
    while (!(*UART_TX_STATUS_ADDRESS &
             (1u << UART_TX_STATUS_IDX_EMPTY)))
        ;

    *UART_BUFFER_ADDRESS = (uint8_t)c;
}


static inline void uart_write_string(const char *s)
{
    while (*s)
        uart_write_char(*s++);
}


static void uart_write_u32(uint32_t v)
{
    char buf[12];
    int i = 0;

    if (v == 0) {
        buf[i++] = '0';
    }
    else {
        while (v) {
            buf[i++] = '0' + (v % 10);
            v /= 10;
        }
    }

    while (i--)
        uart_write_char(buf[i]);
}


/*
 * Receive one byte from UART.
 *
 * This assumes the UART peripheral provides:
 *
 *   UART_RX_STATUS_ADDRESS
 *   UART_RX_STATUS_IDX_FULL
 *
 * and that UART_BUFFER_ADDRESS is used for both TX and RX.
 *
 * If peripherals.h uses different RX register names, only this
 * function needs to be changed.
 */
static inline char uart_read_char(void)
{
    while (!(*UART_RX_STATUS_ADDRESS &
             (1u << UART_RX_STATUS_IDX_FULL)))
        ;

    return (char)(*UART_BUFFER_ADDRESS);
}


/* --------------------------------------------------------------------------
 * GPIO / Motor control
 * -------------------------------------------------------------------------- */

/*
 * Write the complete motor direction state.
 *
 * Only GPIOA[3:0] are modified.
 */
static inline void motor_gpio_write(uint16_t value)
{
    *GPIOA_OUTPUT_ADDRESS =
        (*GPIOA_OUTPUT_ADDRESS & ~MOTOR_GPIO_MASK) |
        (value & MOTOR_GPIO_MASK);
}


/* Stop both motors electrically */
static void motor_stop(void)
{
    motor_gpio_write(0);

    uart_write_string("MOTORS: STOP\r\n");
}


/* Motor A forward, Motor B forward */
static void motor_forward(void)
{
    uint16_t value = 0;

    value |= MOTOR_A_IN1;
    value |= MOTOR_B_IN1;

    motor_gpio_write(value);

    uart_write_string("MOTORS: FORWARD\r\n");
}


/* Motor A backward, Motor B backward */
static void motor_backward(void)
{
    uint16_t value = 0;

    value |= MOTOR_A_IN2;
    value |= MOTOR_B_IN2;

    motor_gpio_write(value);

    uart_write_string("MOTORS: BACKWARD\r\n");
}


/*
 * Turn left:
 *
 * Motor A -> backward
 * Motor B -> forward
 */
static void motor_left(void)
{
    uint16_t value = 0;

    value |= MOTOR_A_IN2;
    value |= MOTOR_B_IN1;

    motor_gpio_write(value);

    uart_write_string("MOTORS: LEFT\r\n");
}


/*
 * Turn right:
 *
 * Motor A -> forward
 * Motor B -> backward
 */
static void motor_right(void)
{
    uint16_t value = 0;

    value |= MOTOR_A_IN1;
    value |= MOTOR_B_IN2;

    motor_gpio_write(value);

    uart_write_string("MOTORS: RIGHT\r\n");
}


/* --------------------------------------------------------------------------
 * PWM control
 * -------------------------------------------------------------------------- */

static uint32_t speed = SPEED_INITIAL;


/* Apply the current speed to both motors */
static void pwm_set_speed(uint32_t duty)
{
    if (duty > SPEED_MAX)
        duty = SPEED_MAX;

    *(PWM_DUTY_BASE_ADDRESS + 0) = duty;
    *(PWM_DUTY_BASE_ADDRESS + 1) = duty;

    speed = duty;
}


/* Increase speed by 10% */
static void speed_increase(void)
{
    if (speed < SPEED_MAX) {
        speed += SPEED_STEP;

        if (speed > SPEED_MAX)
            speed = SPEED_MAX;

        pwm_set_speed(speed);
    }

    uart_write_string("SPEED = ");
    uart_write_u32(speed);
    uart_write_string("%\r\n");
}


/* Decrease speed by 10% */
static void speed_decrease(void)
{
    if (speed > SPEED_MIN) {
        speed -= SPEED_STEP;

        if (speed < SPEED_MIN)
            speed = SPEED_MIN;

        pwm_set_speed(speed);
    }

    uart_write_string("SPEED = ");
    uart_write_u32(speed);
    uart_write_string("%\r\n");
}


/* --------------------------------------------------------------------------
 * PWM initialization
 * -------------------------------------------------------------------------- */

static void pwm_init(void)
{
    /*
     * 9 MHz / (89 + 1)
     *
     * = 100 kHz PWM counter frequency
     */
    *PWM_PRESCALER_ADDRESS = PWM_PRESCALER;

    /*
     * 100 counter ticks per PWM period.
     *
     * 100 kHz / 100 = 1 kHz PWM frequency.
     */
    *PWM_PERIOD_ADDRESS = PWM_PERIOD;

    /* Initial speed */
    *(PWM_DUTY_BASE_ADDRESS + 0) = SPEED_INITIAL;
    *(PWM_DUTY_BASE_ADDRESS + 1) = SPEED_INITIAL;

    /* Enable PWM channels 0 and 1 */
    *PWM_ENABLE_ADDRESS =
        (1u << 0) |
        (1u << 1);

    /* Non-inverted PWM */
    *PWM_INVERT_ADDRESS = 0;

    /* Start global PWM controller */
    *PWM_CTRL_ADDRESS = 1;
}


/* --------------------------------------------------------------------------
 * GPIO initialization
 * -------------------------------------------------------------------------- */

static void gpio_init(void)
{
    /*
     * GPIOA[0:3] -> standard GPIO
     *
     * Assuming FUNC_GPIO = 0x0.
     *
     * GPIOA[4] -> PWM channel 0
     * GPIOA[5] -> PWM channel 1
     *
     * FUNC_PWM = 0x5.
     *
     * FUNC1 controls GPIOA[4:7].
     *
     *   Pin 4 -> bits [3:0] = 0x5
     *   Pin 5 -> bits [7:4] = 0x5
     */
    *GPIOA_FUNC1_ADDRESS =
        (0x5u << 0) |
        (0x5u << 4);

    /* GPIOA[0:3] = GPIO function */
    *GPIOA_FUNC0_ADDRESS = 0;

    /*
     * GPIO mode:
     *
     * 2 bits per pin
     *
     * GPIOA[0] = output -> bits [1:0]
     * GPIOA[1] = output -> bits [3:2]
     * GPIOA[2] = output -> bits [5:4]
     * GPIOA[3] = output -> bits [7:6]
     *
     * MODE_OUTPUT = 0x1
     *
     * Therefore:
     *
     *   0x01
     *   0x04
     *   0x10
     *   0x40
     *
     * = 0x55
     */
    *GPIOA_MODE_ADDRESS =
        (0x1u << 0) |
        (0x1u << 2) |
        (0x1u << 4) |
        (0x1u << 6);

    /* Start with both motors stopped */
    *GPIOA_OUTPUT_ADDRESS &= ~MOTOR_GPIO_MASK;
}


/* --------------------------------------------------------------------------
 * Command processing
 * -------------------------------------------------------------------------- */

static void process_command(char command)
{
    switch (command)
    {
        case 'F':
        case 'f':
            *LEDS_ADDRESS = 0x02;
            motor_forward();
            break;

        case 'B':
        case 'b':
            *LEDS_ADDRESS = 0x04;
            motor_backward();
            break;

        case 'L':
        case 'l':
            *LEDS_ADDRESS = 0x10;
            motor_left();
            break;

        case 'R':
        case 'r':
            *LEDS_ADDRESS = 0x20;
            motor_right();
            break;

        case 'H':
        case 'h':
        case 'S':
        case 's':
            *LEDS_ADDRESS = 0x08;
            motor_stop();
            break;

        case '+':
            speed_increase();
            break;

        case '-':
            speed_decrease();
            break;

        /* Ignore line-ending characters generated by terminal apps */
        case '\r':
        case '\n':
            break;

        default:
            uart_write_string("UNKNOWN COMMAND: ");
            uart_write_char(command);
            uart_write_string("\r\n");
            break;
    }
}


/* --------------------------------------------------------------------------
 * Main
 * -------------------------------------------------------------------------- */

int main(void)
{
    /* Turn off onboard LEDs */
    *LEDS_ADDRESS = 0xFFFF;

    uart_write_string("\r\n");
    uart_write_string("RC CAR DEMO\r\n");

    uart_write_string("Initializing GPIO...\r\n");
    gpio_init();

    uart_write_string("Initializing PWM...\r\n");
    pwm_init();

    /* Start safely in STOP mode, waiting for Bluetooth commands */
    motor_stop();

    uart_write_string("SPEED = ");
    uart_write_u32(speed);
    uart_write_string("%\r\n");

    uart_write_string("\r\n");
    uart_write_string("Bluetooth commands:\r\n");
    uart_write_string("  F = Forward\r\n");
    uart_write_string("  B = Backward\r\n");
    uart_write_string("  L = Left\r\n");
    uart_write_string("  R = Right\r\n");
    uart_write_string("  H = Halt\r\n");
    uart_write_string("  + = Speed up\r\n");
    uart_write_string("  - = Slow down\r\n");
    uart_write_string("\r\n");
    uart_write_string("RC CAR READY\r\n");
    *LEDS_ADDRESS = 0x01; // LED 0 ON: Ready & waiting for BT

    /* Main command loop (Arduino Serial Monitor / Bluetooth) */
    while (1)
    {
        char command = uart_read_char();
        uart_write_string("RECV: '");
        uart_write_char(command);
        uart_write_string("'\r\n");
        process_command(command);
    }

    return 0;
}
