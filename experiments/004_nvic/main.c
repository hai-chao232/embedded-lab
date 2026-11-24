#include "stm32f4xx.h"

/*
 * Experiment 004: NVIC priorities and preemption.
 *
 * 003 had one interrupt source. 004 runs two:
 *   - EXTI13 (B1)          at EXTI_PRIORITY
 *   - SysTick (100 ms)     at SYSTICK_PRIORITY
 *
 * Case A (default): SysTick 0, EXTI 1 -> SysTick preempts the long EXTI ISR.
 * Case B:           swap the two values   -> SysTick waits until EXTI returns.
 *
 * The EXTI ISR deliberately busy-waits ~0.5 s to create a visible
 * preemption window. That is a demo device, not a pattern to copy.
 *
 * Behavior contract: see README "现象".
 */

#define EXTI_PRIORITY    1U
#define SYSTICK_PRIORITY 0U

/*
 * Case B: swap the two lines above (EXTI=0, SysTick=1) and rebuild.
 * Smaller number = higher urgency: 0 preempts 1.
 */

/*
 * Rough ~0.5 s at 16 MHz; exact duration varies with optimization and clock.
 * Long enough for several 100 ms SysTick ticks to arrive inside the window.
 */
#define EXTI_BUSY_LOOPS  1000000U

static volatile uint32_t systick_ticks;

int main(void)
{
    /* GPIOB (LD1/LD2) and GPIOC (B1) are on AHB1; SYSCFG (EXTI mux) is on APB2. */
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN | RCC_AHB1ENR_GPIOCEN;
    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;

    /*
     * Read back the enable registers before touching the peripherals.
     * Besides checking the write path, the volatile reads provide the
     * small synchronization delay required after enabling a clock
     * (same pattern as 001/002/003).
     */
    (void)(RCC->AHB1ENR & (RCC_AHB1ENR_GPIOBEN | RCC_AHB1ENR_GPIOCEN));
    (void)(RCC->APB2ENR & RCC_APB2ENR_SYSCFGEN);

    /* PB0 (LD1) and PB7 (LD2): general-purpose output, push-pull, low speed, no pull. */
    GPIOB->MODER &= ~((3U << (0U * 2U)) | (3U << (7U * 2U)));
    GPIOB->MODER |=  ((1U << (0U * 2U)) | (1U << (7U * 2U)));
    GPIOB->OTYPER &= ~((1U << 0U) | (1U << 7U));
    GPIOB->OSPEEDR &= ~((3U << (0U * 2U)) | (3U << (7U * 2U)));
    GPIOB->PUPDR &= ~((3U << (0U * 2U)) | (3U << (7U * 2U)));

    /*
     * PC13: input (MODER13 = 00), no internal pull (PUPDR13 = 00).
     * The board's R58 (220k) pulls the B1 node low when released, so the
     * pin level is guaranteed by hardware. See 002 README "物理层".
     */
    GPIOC->MODER &= ~(3U << (13U * 2U));
    GPIOC->PUPDR &= ~(3U << (13U * 2U));

    /* LD1 and LD2 off before interrupts are enabled. */
    GPIOB->BSRR = (1U << (0U + 16U)) | (1U << (7U + 16U));

    /*
     * Route EXTI13 to PORTC (PC13). EXTICR4 holds lines 12..15; keep the
     * other fields intact and only rewrite EXTI13[3:0] = 0b0010 (PORTC).
     */
    SYSCFG->EXTICR[3] &= ~SYSCFG_EXTICR4_EXTI13_Msk;
    SYSCFG->EXTICR[3] |=  SYSCFG_EXTICR4_EXTI13_PC;

    /*
     * EXTI13: rising edge only. Press starts one ISR window; release does
     * nothing. RTSR = 1 selects rising, FTSR = 0 disables falling.
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
     * NVIC setup. PRIGROUP = 0 means 4 preemption bits and 0 subpriority
     * bits, so priority values 0 and 1 are both preemption priorities.
     * Smaller value wins: SysTick 0 can preempt EXTI 1 (Case A).
     */
    NVIC_SetPriorityGrouping(0U);
    NVIC_SetPriority(EXTI15_10_IRQn, EXTI_PRIORITY);
    NVIC_SetPriority(SysTick_IRQn, SYSTICK_PRIORITY);

    /*
     * IRQ 40 (EXTI15_10_IRQn) is shared by EXTI lines 10..15; 003 only
     * enables line 13. Clear any stale NVIC pending bit, then enable the
     * vector. SysTick is a core exception and is enabled through
     * SysTick->CTRL.TICKINT below, not through NVIC_EnableIRQ.
     */
    NVIC_ClearPendingIRQ(EXTI15_10_IRQn);
    NVIC_EnableIRQ(EXTI15_10_IRQn);

    /*
     * SysTick: 100 ms heartbeat at the current 16 MHz HSI clock.
     * LOAD is 24-bit; 16 MHz / 10 = 1,600,000 fits. After the 005 clock
     * checkpoint (180 MHz) 100 ms would no longer fit and the tick must
     * be shortened or prescaled. CLKSOURCE=1 selects the processor clock.
     */
    systick_ticks = 0U;
    SysTick->LOAD = (SystemCoreClock / 10U) - 1U;
    SysTick->VAL = 0U;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk
                  | SysTick_CTRL_TICKINT_Msk
                  | SysTick_CTRL_ENABLE_Msk;

    /*
     * Make the EXTI/NVIC/SysTick configuration visible before opening the
     * global interrupt gate.
     */
    __DSB();
    __enable_irq();

    /*
     * The main loop has no work. SysTick wakes it every 100 ms to toggle
     * LD2; B1 wakes it through EXTI13. All real work is in the ISRs.
     */
    while (1)
    {
        __WFI();
    }
}

/*
 * SysTick: 100 ms heartbeat. In Case A it preempts the long EXTI ISR;
 * in Case B it stays pending until the EXTI ISR returns.
 */
void SysTick_Handler(void)
{
    systick_ticks++;

    /* Toggle LD2: read ODR for the current state, then write BSRR atomically. */
    if ((GPIOB->ODR & (1U << 7U)) != 0U)
    {
        GPIOB->BSRR = (1U << (7U + 16U));
    }
    else
    {
        GPIOB->BSRR = (1U << 7U);
    }
}

/*
 * Shared EXTI10..15 vector. Only line 13 is enabled in this experiment.
 *
 * The busy-wait is intentionally long: it holds the CPU in the EXTI ISR
 * long enough for SysTick to arrive and either preempt (Case A) or wait
 * (Case B). A real ISR should be short.
 */
void EXTI15_10_IRQHandler(void)
{
    if ((EXTI->PR & EXTI_PR_PR13) != 0U)
    {
        EXTI->PR = EXTI_PR_PR13;

        /* LD1 on: mark the EXTI ISR window. */
        GPIOB->BSRR = (1U << 0U);

        for (volatile uint32_t i = 0U; i < EXTI_BUSY_LOOPS; ++i)
        {
            __NOP();
        }

        /* LD1 off: window closed. */
        GPIOB->BSRR = (1U << (0U + 16U));
    }
}
