#include "stm32f4xx.h"

/*
 * Experiment 003: EXTI on the user button.
 *
 * 002 made the main loop poll PC13. 003 lets the EXTI hardware detect the
 * rising edge, latch it in PR, and interrupt the CPU through the NVIC.
 * The main loop only sleeps in WFI; the ISR does the minimum work:
 * acknowledge the line and toggle PB0 (LD1).
 *
 * Event path:
 *   PC13 -> SYSCFG_EXTICR4 -> EXTI13 -> NVIC IRQ40
 *        -> EXTI15_10_IRQHandler -> PB0
 *
 * Behavior contract: see README "现象".
 */

int main(void)
{
    /* GPIOB (LD1) / GPIOC (B1) are on AHB1; SYSCFG (EXTI mux) is on APB2. */
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN | RCC_AHB1ENR_GPIOCEN;
    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;

    /*
     * Read back the enable registers before touching the peripherals.
     * Besides checking the write path, the volatile reads provide the
     * small synchronization delay required after enabling a clock
     * (same pattern as 001/002).
     */
    (void)(RCC->AHB1ENR & (RCC_AHB1ENR_GPIOBEN | RCC_AHB1ENR_GPIOCEN));
    (void)(RCC->APB2ENR & RCC_APB2ENR_SYSCFGEN);

    /* PB0: general-purpose output (MODER0 = 01), push-pull, low speed, no pull. */
    GPIOB->MODER &= ~(3U << (0U * 2U));
    GPIOB->MODER |=  (1U << (0U * 2U));
    GPIOB->OTYPER &= ~(1U << 0U);
    GPIOB->OSPEEDR &= ~(3U << (0U * 2U));
    GPIOB->PUPDR &= ~(3U << (0U * 2U));

    /*
     * PC13: input (MODER13 = 00), no internal pull (PUPDR13 = 00).
     * The board's R58 (220k) pulls the B1 node low when released, so the
     * pin level is guaranteed by hardware. See 002 README "物理层".
     */
    GPIOC->MODER &= ~(3U << (13U * 2U));
    GPIOC->PUPDR &= ~(3U << (13U * 2U));

    /* LD1 off before interrupts are enabled. */
    GPIOB->BSRR = (1U << (0U + 16U));

    /*
     * Route EXTI13 to PORTC (PC13). EXTICR4 holds lines 12..15; keep the
     * other fields intact and only rewrite EXTI13[3:0] = 0b0010 (PORTC).
     */
    SYSCFG->EXTICR[3] &= ~SYSCFG_EXTICR4_EXTI13_Msk;
    SYSCFG->EXTICR[3] |=  SYSCFG_EXTICR4_EXTI13_PC;

    /*
     * EXTI13: rising edge only. Press toggles; release does nothing.
     * RTSR = 1 selects rising, FTSR = 0 disables falling.
     */
    EXTI->RTSR |= EXTI_RTSR_TR13;
    EXTI->FTSR &= ~EXTI_FTSR_TR13;

    /*
     * Clear any stale pending bit before unmasking. PR is write-1-to-clear:
     * writing 0 has no effect, so never use "PR &= ~PR13" here.
     */
    EXTI->PR = EXTI_PR_PR13;

    /* IMR: 1 = unmasked, 0 = masked. Enable line 13. */
    EXTI->IMR |= EXTI_IMR_IM13;

    /*
     * IRQ 40 (EXTI15_10_IRQn) is shared by EXTI lines 10..15; 003 only
     * enables line 13. Clear any stale NVIC pending bit, then enable the
     * vector. Priority stays at its reset default; 004 explores priorities.
     */
    NVIC_ClearPendingIRQ(EXTI15_10_IRQn);
    NVIC_EnableIRQ(EXTI15_10_IRQn);

    /*
     * Ensure the EXTI/NVIC configuration above is visible before opening
     * the global interrupt gate.
     */
    __DSB();
    __enable_irq();

    /*
     * Event-driven main loop: no polling, no delay, no work. WFI stops the
     * CPU until an enabled interrupt arrives; the ISR below handles it.
     */
    while (1)
    {
        __WFI();
    }
}

/*
 * Shared EXTI10..15 vector. Only line 13 is enabled in this experiment,
 * but check PR13 explicitly instead of assuming the interrupt source.
 */
void EXTI15_10_IRQHandler(void)
{
    if ((EXTI->PR & EXTI_PR_PR13) != 0U)
    {
        /*
         * Acknowledge first. PR is write-1-to-clear; if a new edge arrives
         * after this write, PR sets again and the NVIC re-enters after
         * this ISR returns.
         */
        EXTI->PR = EXTI_PR_PR13;

        /*
         * Toggle LD1: ODR tells the current output state; BSRR then does an
         * atomic set/reset without a read-modify-write race. BSRR is
         * write-only, so the state must come from ODR.
         */
        if ((GPIOB->ODR & (1U << 0U)) != 0U)
        {
            GPIOB->BSRR = (1U << (0U + 16U));
        }
        else
        {
            GPIOB->BSRR = (1U << 0U);
        }
    }
}
