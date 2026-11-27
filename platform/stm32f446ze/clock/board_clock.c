/*
 * platform/stm32f446ze/clock/board_clock.c
 *
 * HSE bypass 8 MHz -> PLL -> 180 MHz, with the bus prescalers required for
 * that frequency. See board_clock.h for the clock tree and the constraints.
 *
 * The vendor system_stm32f4xx.c in third_party/STM32CubeF4 is the CMSIS
 * template: its SystemInit() only enables the FPU and the vector table and
 * leaves RCC at reset (HSI 16 MHz, PLL off). That is why this module can own
 * the whole clock tree without fighting a Cube-generated SetSysClock().
 *
 * Sequence (RM0390: PWR chapter for VOS, RCC chapter for PLL/prescalers,
 * FLASH chapter for ACR):
 *   1. SYSCLK = HSI, PLL off         -- known state, safe for a 2nd call
 *   2. PWR: VOS = scale 1            -- latched now, effective once the PLL runs
 *   3. FLASH: 5 WS + prefetch/caches -- must be set while SYSCLK is still low
 *   4. HSE: bypass on, HSE on        -- 8 MHz from the ST-LINK MCO
 *   5. PLL: M/N/P/Q from board_clock.h, wait PLLRDY
 *   6. wait VOSRDY                   -- F446: regulator stays at scale 3 until PLL on
 *   7. AHB /1, APB1 /4, APB2 /2      -- before SYSCLK moves
 *   8. SYSCLK = PLL, refresh SystemCoreClock, read the tree back
 */

#include "board_clock.h"

#include "stm32f4xx.h"

/* PLLP field encoding: 00 -> /2, 01 -> /4, 10 -> /6, 11 -> /8. */
#define PLL_P_FIELD  ((BOARD_PLL_P / 2U) - 1U)

/*
 * Busy-wait bound for "wait until an RCC/PWR flag changes". HSE start-up is
 * specified in the 100 us range and PLL lock in the same order; at 16 MHz HSI
 * this loop is tens of milliseconds, i.e. a real timeout rather than a spin
 * that can never end. At 180 MHz it is still several milliseconds.
 */
#define SPIN_LIMIT  (200000U)

/* ------------------------------------------------------------------------- */
/* Compile-time contract                                                      */
/*                                                                            */
/* If any of these fails, the board clock is no longer valid for the          */
/* experiments that depend on it, and the build stops with the message below  */
/* instead of producing firmware that silently runs at the wrong frequency.   */
/* ------------------------------------------------------------------------- */

/* The ST-LINK MCO is 8 MHz; HSE_VALUE is injected by platform CMakeLists. */
_Static_assert(HSE_VALUE == BOARD_HSE_HZ,
               "HSE_VALUE must match the 8 MHz ST-LINK MCO (HSE bypass)");

/* PLL input must be 1..2 MHz (2 MHz recommended). */
_Static_assert((BOARD_PLL_VCO_IN_HZ >= 1000000U) && (BOARD_PLL_VCO_IN_HZ <= 2000000U),
               "PLL VCO input must stay within 1..2 MHz");

/* VCO output must be 100..432 MHz, and PLLN must be 50..432. */
_Static_assert((BOARD_PLL_VCO_HZ >= 100000000U) && (BOARD_PLL_VCO_HZ <= 432000000U),
               "PLL VCO output must stay within 100..432 MHz");
_Static_assert((BOARD_PLL_N >= 50U) && (BOARD_PLL_N <= 432U),
               "PLLN must stay within 50..432");
_Static_assert((BOARD_PLL_M >= 2U) && (BOARD_PLL_M <= 63U),
               "PLLM must stay within 2..63");
_Static_assert((BOARD_PLL_Q >= 2U) && (BOARD_PLL_Q <= 15U),
               "PLLQ must stay within 2..15");
_Static_assert((BOARD_PLL_P == 2U) || (BOARD_PLL_P == 4U) ||
               (BOARD_PLL_P == 6U) || (BOARD_PLL_P == 8U),
               "PLLP must be 2, 4, 6 or 8");

/* STM32F446 worst-case value, and the value the experiments were written for. */
_Static_assert(BOARD_SYSCLK_HZ <= 180000000U, "SYSCLK above 180 MHz is out of spec");
_Static_assert(BOARD_SYSCLK_HZ == 180000000U,
               "004/005/006 baseline is 180 MHz SYSCLK, 45 MHz PCLK1, 90 MHz TIMxCLK");

/* Flash wait states: 5 WS is the table entry for 150..180 MHz at VOS1. */
_Static_assert(BOARD_FLASH_LATENCY == 5U,
               "180 MHz at VOS scale 1 needs 5 Flash wait states");

/* Timer clock bookkeeping used by TIM7 (005) and TIM1..5 (006). */
_Static_assert(BOARD_APB1_TIMER_HZ == 90000000U, "TIM7 kernel clock must be 90 MHz");
_Static_assert(BOARD_APB2_TIMER_HZ == 180000000U, "APB2 timer clock must be 180 MHz");

/* ------------------------------------------------------------------------- */
/* Small wait helpers                                                         */
/* ------------------------------------------------------------------------- */

/* Wait until (*reg & mask) != 0. Returns 1 on success, 0 on timeout. */
static uint32_t wait_set(volatile uint32_t *reg, uint32_t mask)
{
    for (uint32_t spin = 0U; spin < SPIN_LIMIT; ++spin)
    {
        if ((*reg & mask) != 0U)
        {
            return 1U;
        }
    }

    return 0U;
}

/* Wait until (*reg & mask) == 0. Returns 1 on success, 0 on timeout. */
static uint32_t wait_clear(volatile uint32_t *reg, uint32_t mask)
{
    for (uint32_t spin = 0U; spin < SPIN_LIMIT; ++spin)
    {
        if ((*reg & mask) == 0U)
        {
            return 1U;
        }
    }

    return 0U;
}

/* Wait until CFGR.SWS reports the given multiplexer value. */
static uint32_t wait_sws(uint32_t sws)
{
    for (uint32_t spin = 0U; spin < SPIN_LIMIT; ++spin)
    {
        if ((RCC->CFGR & RCC_CFGR_SWS) == sws)
        {
            return 1U;
        }
    }

    return 0U;
}

/* ------------------------------------------------------------------------- */
/* Programming                                                                */
/* ------------------------------------------------------------------------- */

static board_clock_status_t clock_program_tree(void)
{
    /*
     * 1. Back to a known state: SYSCLK on HSI, PLL stopped, HSE stopped.
     *    board_clock_init() must be callable from reset *and* from a running
     *    180 MHz configuration, so this comes before any other write.
     */
    RCC->CR |= RCC_CR_HSION;

    if (wait_set(&RCC->CR, RCC_CR_HSIRDY) == 0U)
    {
        return BOARD_CLOCK_ERR_HSI_TIMEOUT;
    }

    RCC->CFGR &= ~RCC_CFGR_SW;  /* SW = 00 -> HSI */

    if (wait_sws(RCC_CFGR_SWS_HSI) == 0U)
    {
        return BOARD_CLOCK_ERR_SYSCLK_SWITCH;
    }

    RCC->CR &= ~RCC_CR_PLLON;
    (void)wait_clear(&RCC->CR, RCC_CR_PLLRDY);

    RCC->CR &= ~RCC_CR_HSEON;
    (void)wait_clear(&RCC->CR, RCC_CR_HSERDY);

    /*
     * 2. Regulator scale 1 (up to 180 MHz). PWR sits on APB1.
     *
     * STM32F446 quirk, observed on the board and documented by the vendor HAL
     * (stm32f4xx_hal_pwr_ex.c, HAL_PWREx_ControlVoltageScaling):
     * "When the PLL is OFF, the voltage scale 3 is automatically selected and
     *  the VOS bits value is not taken into account. The new voltage scale is
     *  active only when the PLL is ON."
     *
     * So writing VOS here only latches the value; VOSRDY stays 0 until the PLL
     * has locked (step 6). Do NOT wait for VOSRDY at this point -- that is the
     * trap this module hit first on hardware.
     */
    RCC->APB1ENR |= RCC_APB1ENR_PWREN;
    (void)(RCC->APB1ENR & RCC_APB1ENR_PWREN);

    PWR->CR |= PWR_CR_VOS;  /* VOS[1:0] = 11 -> scale 1 */

    /*
     * 3. Five Flash wait states, prefetch and both caches. The latency must be
     *    raised before the frequency goes up; raising it too late is a hard
     *    fault class of bug.
     */
    FLASH->ACR = FLASH_ACR_LATENCY_5WS
               | FLASH_ACR_PRFTEN
               | FLASH_ACR_ICEN
               | FLASH_ACR_DCEN;

    if ((FLASH->ACR & FLASH_ACR_LATENCY_Msk) != FLASH_ACR_LATENCY_5WS)
    {
        return BOARD_CLOCK_ERR_FLASH_LATENCY;
    }

    /*
     * 4. HSE in bypass mode. The 8 MHz square wave comes from the ST-LINK
     *    MCO; there is no crystal and no load capacitors on the target side.
     *    HSEBYP is writable only while HSEON = 0, hence the order above.
     */
    RCC->CR |= RCC_CR_HSEBYP;
    RCC->CR |= RCC_CR_HSEON;

    if (wait_set(&RCC->CR, RCC_CR_HSERDY) == 0U)
    {
        return BOARD_CLOCK_ERR_HSE_TIMEOUT;
    }

    /* 5. PLL factors. PLLCFGR is writable only while the PLL is off. */
    RCC->PLLCFGR = (BOARD_PLL_M << RCC_PLLCFGR_PLLM_Pos)
                 | (BOARD_PLL_N << RCC_PLLCFGR_PLLN_Pos)
                 | (PLL_P_FIELD << RCC_PLLCFGR_PLLP_Pos)
                 | RCC_PLLCFGR_PLLSRC_HSE
                 | (BOARD_PLL_Q << RCC_PLLCFGR_PLLQ_Pos);

    RCC->CR |= RCC_CR_PLLON;

    if (wait_set(&RCC->CR, RCC_CR_PLLRDY) == 0U)
    {
        return BOARD_CLOCK_ERR_PLL_TIMEOUT;
    }

    /*
     * 6. Now that the PLL runs, the regulator leaves scale 3 and VOSRDY
     *    finally reports the scale 1 we asked for in step 2. The bus is still
     *    at HSI, so the regulator has plenty of time to settle before HCLK
     *    is raised.
     */
    if (wait_set(&PWR->CSR, PWR_CSR_VOSRDY) == 0U)
    {
        return BOARD_CLOCK_ERR_REGULATOR;
    }

    /*
     * 7. Bus prescalers. They must be in place *before* SYSCLK moves: at
     *    180 MHz HCLK, APB1 /1 would run PCLK1 at 180 MHz (max 45 MHz).
     */
    RCC->CFGR = (RCC->CFGR & ~(RCC_CFGR_HPRE | RCC_CFGR_PPRE1 | RCC_CFGR_PPRE2))
              | RCC_CFGR_HPRE_DIV1
              | RCC_CFGR_PPRE1_DIV4
              | RCC_CFGR_PPRE2_DIV2;

    /* 8. Final switch. SWS is the hardware's confirmation, SW is the request. */
    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_PLL;

    if (wait_sws(RCC_CFGR_SWS_PLL) == 0U)
    {
        return BOARD_CLOCK_ERR_SYSCLK_SWITCH;
    }

    return BOARD_CLOCK_OK;
}

/* Readback of the tree that the hardware actually reports. */
static board_clock_status_t clock_verify_tree(void)
{
    if ((RCC->CFGR & RCC_CFGR_HPRE) != RCC_CFGR_HPRE_DIV1)
    {
        return BOARD_CLOCK_ERR_TREE_MISMATCH;
    }

    if ((RCC->CFGR & RCC_CFGR_PPRE1) != RCC_CFGR_PPRE1_DIV4)
    {
        return BOARD_CLOCK_ERR_TREE_MISMATCH;
    }

    if ((RCC->CFGR & RCC_CFGR_PPRE2) != RCC_CFGR_PPRE2_DIV2)
    {
        return BOARD_CLOCK_ERR_TREE_MISMATCH;
    }

    /*
     * SystemCoreClock is HCLK, computed by SystemCoreClockUpdate() from
     * HSE_VALUE and the PLL registers. A mismatch here means the PLL or the
     * AHB prescaler is not what this module asked for.
     */
    if (SystemCoreClock != BOARD_HCLK_HZ)
    {
        return BOARD_CLOCK_ERR_TREE_MISMATCH;
    }

    if (board_clock_apb1_timer_hz() != BOARD_APB1_TIMER_HZ)
    {
        return BOARD_CLOCK_ERR_TREE_MISMATCH;
    }

    return BOARD_CLOCK_OK;
}

/* ------------------------------------------------------------------------- */
/* Public API                                                                 */
/* ------------------------------------------------------------------------- */

board_clock_status_t board_clock_init(void)
{
    board_clock_status_t status = clock_program_tree();

    /*
     * Refresh SystemCoreClock on every path, including a failed one: the
     * variable must always describe the registers, or later code would compute
     * timer values from a stale frequency.
     */
    SystemCoreClockUpdate();

    if (status != BOARD_CLOCK_OK)
    {
        return status;
    }

    return clock_verify_tree();
}

uint32_t board_clock_hclk_hz(void)
{
    /* SystemCoreClock is HCLK (AHB prescaler already applied). */
    return SystemCoreClock;
}

/* PPRE[2:0] encoding: 0xx -> /1, 100 -> /2, 101 -> /4, 110 -> /8, 111 -> /16. */
static uint32_t apb_divider(uint32_t ppre_mask, uint32_t ppre_pos)
{
    static const uint8_t divider[8] = { 1U, 1U, 1U, 1U, 2U, 4U, 8U, 16U };

    return (uint32_t)divider[(RCC->CFGR & ppre_mask) >> ppre_pos];
}

static uint32_t apb_pclk_hz(uint32_t ppre_mask, uint32_t ppre_pos)
{
    return board_clock_hclk_hz() / apb_divider(ppre_mask, ppre_pos);
}

static uint32_t apb_timer_hz(uint32_t ppre_mask, uint32_t ppre_pos)
{
    uint32_t divider = apb_divider(ppre_mask, ppre_pos);
    uint32_t pclk    = board_clock_hclk_hz() / divider;

    /*
     * The x2 rule is easy to get wrong: APB1 /4 gives 45 MHz, but TIM7 runs
     * at 90 MHz. With APB /1 the timer clock is PCLK1 itself, not 2x PCLK1.
     */
    return (divider == 1U) ? pclk : (pclk * 2U);
}

uint32_t board_clock_pclk1_hz(void)
{
    return apb_pclk_hz(RCC_CFGR_PPRE1, RCC_CFGR_PPRE1_Pos);
}

uint32_t board_clock_pclk2_hz(void)
{
    return apb_pclk_hz(RCC_CFGR_PPRE2, RCC_CFGR_PPRE2_Pos);
}

uint32_t board_clock_apb1_timer_hz(void)
{
    return apb_timer_hz(RCC_CFGR_PPRE1, RCC_CFGR_PPRE1_Pos);
}

uint32_t board_clock_apb2_timer_hz(void)
{
    return apb_timer_hz(RCC_CFGR_PPRE2, RCC_CFGR_PPRE2_Pos);
}
