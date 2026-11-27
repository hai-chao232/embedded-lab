#include "board_clock.h"

#include "stm32f4xx.h"

/*
 * Experiment 005: the first tick that the MCU generates by itself.
 *
 * 001-004 reacted to the outside world (write a pin, read a pin, take an edge,
 * arbitrate two interrupts). 005 needs no button at all: a basic timer counts
 * the APB1 clock and raises an update interrupt every millisecond, and the
 * whole program becomes "sleep until the next tick".
 *
 * Timebase chain:
 *   platform clock (HSE 8 MHz -> PLL -> 180 MHz, APB1 /4)
 *     -> TIM7CLK 90 MHz
 *     -> PSC 89  -> counter 1 MHz
 *     -> ARR 999 -> UEV every 1 ms
 *     -> UIF -> NVIC IRQ55 -> TIM7_IRQHandler
 *     -> ms_ticks++ and a /500 divider toggling LD1
 *
 * TIM7 is a *basic* timer on purpose: no channels, no input capture, so the
 * only things on screen are PSC / ARR / CNT / UEV / UIF / DIER / NVIC / ISR.
 * Channels (CCMR / CCR / CCER) arrive with 006.
 *
 * Behavior contract: see README "现象".
 */

/* ------------------------------------------------------------------------- */
/* Time base                                                                  */
/*                                                                            */
/*   update rate = TIM7CLK / ((PSC + 1) * (ARR + 1))                          */
/*               = 90 MHz / (90 * 1000) = 1 kHz -> one update every 1 ms      */
/*                                                                            */
/* PSC and ARR are deliberately hard-coded instead of derived from            */
/* board_clock_apb1_timer_hz(): the 1 s LED period is the *measuring           */
/* instrument* for the Clock checkpoint. If the platform clock is wrong, this  */
/* period is wrong in proportion (2x, 4x), which is exactly the symptom the    */
/* README asks you to interpret. Deriving PSC from the library would silently  */
/* hide a clock-tree bug.                                                     */
/*                                                                            */
/* Both registers are 16 bit (0..65535), which is why one divider is not       */
/* enough: 90 MHz / 1000 would need PSC = 89999. Two stages instead:           */
/*   /90 -> 1 MHz counter (PSC fits), then /1000 -> 1 kHz update (ARR fits).   */
/* ------------------------------------------------------------------------- */

#define TIM7_PSC            (89U)   /* 90 MHz / 90   = 1 MHz counter clock   */
#define TIM7_ARR            (999U)  /* 1 MHz / 1000  = 1 kHz update          */

#define LED_TOGGLE_TICKS    (500U)  /* 500 x 1 ms = 500 ms per toggle -> 1 Hz */

/*
 * 005 has a single interrupt source, so the numeric priority does not decide
 * anything here; 004 already covered preemption. Keep it out of the reset
 * value so the value is visible in the debugger and deliberate.
 */
#define TIM7_IRQ_PRIORITY   (1U)

_Static_assert(TIM7_PSC <= 0xFFFFU, "TIM7 PSC is a 16-bit register");
_Static_assert(TIM7_ARR <= 0xFFFFU, "TIM7 ARR is a 16-bit register");

/*
 * 1 ms software tick. Not static on purpose: "watch ms_ticks" must work in the
 * debugger without file-scope qualification. Wraps after ~49.7 days.
 */
volatile uint32_t ms_ticks;

/* 500-tick divider for LD1, kept separate so the tick itself stays 1 ms. */
static uint16_t led_divider;

/*
 * Result of the Clock checkpoint. Read it in the debugger when LD3 is on: it
 * tells which step of board_clock_init() failed.
 */
static volatile board_clock_status_t clock_status;

int main(void)
{
    /* Clock checkpoint first: every number below assumes 180 MHz SYSCLK. */
    clock_status = board_clock_init();

    /* GPIOB holds LD1 (PB0) and LD3 (PB14); it is clocked from AHB1. */
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN;

    /*
     * Read back the enable bit before touching GPIOB. Besides checking the
     * write path, the volatile read provides the small synchronization delay
     * required after enabling an RCC peripheral clock (same pattern as 001).
     */
    (void)(RCC->AHB1ENR & RCC_AHB1ENR_GPIOBEN);

    /* PB0 (LD1) and PB14 (LD3): output, push-pull, low speed, no pull. */
    GPIOB->MODER &= ~((3U << (0U * 2U)) | (3U << (14U * 2U)));
    GPIOB->MODER |=  ((1U << (0U * 2U)) | (1U << (14U * 2U)));
    GPIOB->OTYPER &= ~((1U << 0U) | (1U << 14U));
    GPIOB->OSPEEDR &= ~((3U << (0U * 2U)) | (3U << (14U * 2U)));
    GPIOB->PUPDR &= ~((3U << (0U * 2U)) | (3U << (14U * 2U)));

    /* Both LEDs off before anything else happens. */
    GPIOB->BSRR = (1U << (0U + 16U)) | (1U << (14U + 16U));

    if (clock_status != BOARD_CLOCK_OK)
    {
        /*
         * Clock checkpoint failed: the MCU is still running from HSI at
         * 16 MHz, so the CPU is alive but every timing number below would be
         * meaningless. LD3 (red) stays lit as a visible fault flag, and
         * clock_status says which step failed. No WFI wake source is enabled,
         * so the core idles here until the next reset.
         */
        GPIOB->BSRR = (1U << 14U);

        while (1)
        {
            __WFI();
        }
    }

    /* TIM7 is an APB1 peripheral; its kernel clock is PCLK1 x2 = 90 MHz. */
    RCC->APB1ENR |= RCC_APB1ENR_TIM7EN;
    (void)(RCC->APB1ENR & RCC_APB1ENR_TIM7EN);

    /* Start from a known state: counter stopped, no update interrupt. */
    TIM7->CR1  = 0U;
    TIM7->DIER = 0U;

    TIM7->PSC = TIM7_PSC;
    TIM7->ARR = TIM7_ARR;
    TIM7->CNT = 0U;

    /*
     * EGR.UG generates an update event by software: PSC and ARR are loaded
     * into their active (shadow) registers and the counter restarts from 0.
     * Side effect: UG also sets UIF. Clear it *before* enabling UIE, otherwise
     * the first interrupt fires the moment the global gate opens, 1 ms early.
     */
    TIM7->EGR = TIM_EGR_UG;
    TIM7->SR  = 0U;     /* UIF is rc_w0: write 0 clears it, write 1 does nothing */

    TIM7->DIER = TIM_DIER_UIE;

    /* IRQ 55 -> NVIC->ISER[1] bit 23, shareable vector dedicated to TIM7. */
    NVIC_SetPriority(TIM7_IRQn, TIM7_IRQ_PRIORITY);
    NVIC_ClearPendingIRQ(TIM7_IRQn);
    NVIC_EnableIRQ(TIM7_IRQn);

    ms_ticks    = 0U;
    led_divider = 0U;

    /* Start the counter. UEV arrives every 1 ms from here on. */
    TIM7->CR1 = TIM_CR1_CEN;

    /* Make the TIM7/NVIC configuration visible before opening the IRQ gate. */
    __DSB();
    __enable_irq();

    /*
     * No polling, no software delay: the CPU sleeps and is woken by the 1 ms
     * update interrupt. Every visible effect is produced inside the ISR.
     */
    while (1)
    {
        __WFI();
    }
}

/*
 * TIM7 update interrupt, once per millisecond.
 *
 * Budget: the ISR has 1 ms of CPU time before the next update arrives. It
 * clears UIF, counts a tick and toggles a pin, i.e. a few dozen cycles. The
 * fault injection in the README stretches this on purpose to show what
 * happens when the budget is exceeded: UIF is a single flag, so a late tick is
 * lost, not queued, and the LED period grows.
 */
void TIM7_IRQHandler(void)
{
    /*
     * Check the flag instead of assuming the source. The vector belongs to
     * TIM7 alone, but the flag check is the habit that matters as soon as a
     * vector is shared (EXTI15_10 in 003/004).
     */
    if ((TIM7->SR & TIM_SR_UIF) == 0U)
    {
        return;
    }

    /* UIF is rc_w0: write 0 to clear. Leaving it set re-enters forever. */
    TIM7->SR &= ~TIM_SR_UIF;

    ++ms_ticks;

    if (++led_divider >= LED_TOGGLE_TICKS)
    {
        led_divider = 0U;

        /*
         * Toggle LD1: ODR tells the current state, BSRR then sets or resets
         * atomically. BSRR is write-only, so the state must come from ODR.
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
