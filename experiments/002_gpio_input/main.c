#include "stm32f4xx.h"

/*
 * Experiment 002: gpio_input, polling.
 *
 * The read path is the mirror image of experiment 001's write path:
 * the physical world is the cause (B1 -> PC13 level -> IDR sample),
 * software only observes, then maps the observation back onto PB0/LD1.
 * Behavior contract: see README "现象".
 */
int main(void)
{
    /* GPIOB (LD1) and GPIOC (B1) are clocked from AHB1. */
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN | RCC_AHB1ENR_GPIOCEN;

    /*
     * Read back the enable bits before touching the GPIOs.
     * Besides checking the write path, the volatile read provides the small
     * synchronization delay required after enabling an RCC peripheral clock.
     */
    (void)(RCC->AHB1ENR & (RCC_AHB1ENR_GPIOBEN | RCC_AHB1ENR_GPIOCEN));

    /* PB0: general-purpose output (MODER0 = 01), same as experiment 001. */
    GPIOB->MODER &= ~(3U << (0U * 2U));
    GPIOB->MODER |=  (1U << (0U * 2U));

    /* PB0: push-pull (OT0 = 0). */
    GPIOB->OTYPER &= ~(1U << 0U);

    /* PB0: low output speed (OSPEEDR0 = 00). */
    GPIOB->OSPEEDR &= ~(3U << (0U * 2U));

    /* PB0: no pull-up / no pull-down (PUPDR0 = 00). */
    GPIOB->PUPDR &= ~(3U << (0U * 2U));

    /* PC13: input mode (MODER13 = 00), the reset default, written explicitly. */
    GPIOC->MODER &= ~(3U << (13U * 2U));

    /*
     * PC13: no internal pull (PUPDR13 = 00). Deliberate, not an oversight:
     * the board already pulls the B1 node down through R58 (220k) when the
     * button is released (MB1137 schematic, sheet 2), so the pin level is
     * guaranteed by hardware, not by software. See README "物理层".
     */
    GPIOC->PUPDR &= ~(3U << (13U * 2U));

    /* Initial state per README: LD1 off until the button is pressed. */
    GPIOB->BSRR = (1U << (0U + 16U));

    while (1)
    {
        /*
         * Sample, don't store: IDR reflects the pin level at the instant of
         * the read. Events between two reads are invisible to software --
         * the inherent limitation of polling, kept as the contrast for 003.
         */
        if ((GPIOC->IDR & (1U << 13U)) != 0U)
        {
            /* B1 pressed (active high): LD1 on. BSRR set is atomic. */
            GPIOB->BSRR = (1U << 0U);
        }
        else
        {
            /* B1 released (R58 pulls low): LD1 off. BSRR reset is atomic. */
            GPIOB->BSRR = (1U << (0U + 16U));
        }
    }
}
