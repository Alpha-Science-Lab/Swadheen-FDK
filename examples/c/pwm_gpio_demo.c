/* File: pwm_gpio_demo.c
 * Demonstration firmware for PWM and GPIO output on Port A.
 *
 * Requirements:
 *  - GPIOA[6:4] configured as PWM outputs (Channels 0, 1, 2)
 *  - GPIOA[7] configured as standard GPIO Output
 *  - PWM Duty Cycle ramps from 20% to 100% and back to 20% with step size of 10
 *  - GPIOA[7] toggles state every 0.5 seconds
 */

#include <stdint.h>
#include "peripherals.h"

#define SYS_CLK_HZ        9000000UL
#define HALF_SECOND_TICKS (SYS_CLK_HZ / 2)

/* Read 64-bit hardware timer */
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

/* Delay for 0.5 seconds using hardware mtime */
static void delay_half_second(void)
{
    uint64_t start = timer_get();

    while ((timer_get() - start) < HALF_SECOND_TICKS)
        ;
}

int main(void)
{
    *LEDS_ADDRESS = 0xFFFF; /* Turn off onboad LEDs */

    /* ------------------------------------------------------------------------
     * GPIO Configuration
     * ------------------------------------------------------------------------
     * REGA_FUNC1 (controlling GPIOA pins 4 to 7):
     *   - Pin 4: FUNC_PWM (0x5) -> PWM Ch 0
     *   - Pin 5: FUNC_PWM (0x5) -> PWM Ch 1
     *   - Pin 6: FUNC_PWM (0x5) -> PWM Ch 2
     *   - Pin 7: FUNC_GPIO (0x0) -> GPIO
     *
     * REGA_MODE (controlling GPIOA pin modes, 2 bits per pin):
     *   - Pin 7 mode set to MODE_OUTPUT (0x1) -> bits [15:14] = 2'b01
     * ------------------------------------------------------------------------
     */
    uint32_t func1_val = *GPIOA_FUNC1_ADDRESS;
    func1_val &= ~0x0000FFFFu; // Clear pins 4..7 function nibbles
    func1_val |= (0x0u << 12) | (0x5u << 8) | (0x5u << 4) | (0x5u << 0);
    *GPIOA_FUNC1_ADDRESS = func1_val;

    uint32_t mode_val = *GPIOA_MODE_ADDRESS;
    mode_val &= ~(0x3u << 14); // Clear pin 7 mode bits [15:14]
    mode_val |=  (0x1u << 14); // Set pin 7 mode to MODE_OUTPUT (0x1)
    *GPIOA_MODE_ADDRESS = mode_val;

    /* ------------------------------------------------------------------------
     * PWM Peripheral Configuration
     * ------------------------------------------------------------------------
     *  - Prescaler: 89 (for 9MHz clock, tick frequency = 100 kHz)
     *  - Period: 100 (100 ticks per PWM cycle = 1 kHz PWM frequency)
     *  - Enable channels 0, 1, 2 (bits [2:0] = 0x7)
     *  - Enable PWM global controller (*PWM_CTRL_ADDRESS = 1)
     * ------------------------------------------------------------------------
     */
     
    *PWM_PRESCALER_ADDRESS = 89;
    *PWM_PERIOD_ADDRESS    = 100;

    /* Enable PWM channels 0, 1, and 2 */
    *PWM_ENABLE_ADDRESS    = (1u << 0) | (1u << 1) | (1u << 2);
    *PWM_INVERT_ADDRESS    = 0;

    /* Start global PWM counter */
    *PWM_CTRL_ADDRESS      = 1;

    /* Initial variables */
    uint32_t duty = 20;
    int dir = 1; // 1 for incrementing, -1 for decrementing
    uint16_t gpioa_out = 0;

    while (1) {
        /* Update duty cycles for PWM Channels 0, 1, and 2 */
        *(PWM_DUTY_BASE_ADDRESS + 0) = duty;
        *(PWM_DUTY_BASE_ADDRESS + 1) = duty;
        *(PWM_DUTY_BASE_ADDRESS + 2) = duty;

        /* Toggle GPIOA[7] */
        gpioa_out ^= (1u << 7);
        *GPIOA_OUTPUT_ADDRESS = gpioa_out;

        /* Half-second delay */
        delay_half_second();

        /* Update duty cycle: ramp between 20 and 100 with step size 10 */
        if (dir == 1) {
            duty += 10;
            if (duty >= 100) {
                duty = 100;
                dir = -1;
            }
        } else {
            duty -= 10;
            if (duty <= 20) {
                duty = 20;
                dir = 1;
            }
        }
    }

    return 0;
}
