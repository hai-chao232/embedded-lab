/*
 * platform/stm32f446ze/clock/board_clock.h
 *
 * NUCLEO-F446ZE clock baseline -- the "Clock checkpoint" between 004 and 005.
 *
 * The board has no crystal on the target MCU: the on-board ST-LINK/V2-1 drives
 * 8 MHz into PH0/OSC_IN, so HSE must run in *bypass* mode (HSEBYP = 1), not in
 * crystal mode. From there the PLL produces the 180 MHz worst-case SYSCLK of
 * the STM32F446:
 *
 *   HSE 8 MHz --/M=4--> 2 MHz --xN=180--> VCO 360 MHz --/P=2--> SYSCLK 180 MHz
 *                                             |
 *                                             +--/Q=7--> 51.43 MHz (see note)
 *
 *   SYSCLK 180 MHz --AHB  /1--> HCLK  180 MHz
 *                  --APB1 /4--> PCLK1  45 MHz --x2--> TIM2/3/4/5/6/7/12/13/14 = 90 MHz
 *                  --APB2 /2--> PCLK2  90 MHz --x2--> TIM1/8/9/10/11           = 180 MHz
 *
 * Constraints this configuration satisfies (RM0390 RCC chapter):
 *   - PLL input (VCO input) must be 1..2 MHz  -> 8 MHz / 4 = 2 MHz (the
 *     recommended value; less jitter than 8 MHz / 8 = 1 MHz)
 *   - VCO output must be 100..432 MHz         -> 360 MHz
 *   - PLLN 50..432, PLLP in {2,4,6,8}, PLLQ 2..15
 *   - SYSCLK <= 180 MHz at VOS scale 1, with 5 Flash wait states
 *
 * Note on PLLQ: 360 MHz / 7 = 51.43 MHz, not 48 MHz. The 48 MHz USB OTG FS
 * clock cannot be derived from this PLL while SYSCLK stays at 180 MHz; the
 * F446 solves that with PLLSAI (Q = 8) in the USB experiments. 005 does not
 * need PLLQ at all; it is programmed to a legal value so the field is never
 * left at a random setting.
 *
 * What this module does NOT do (deliberate, single cognitive point per step):
 *   - no CSS (clock security system): an HSE failure on the board shows up as
 *     a wrong LED period, which 005 is designed to reveal;
 *   - no PLLSAI/PLLI2S, no MCO output, no sleep-mode clock gating;
 *   - no per-peripheral clock enable: experiments enable their own RCC bits.
 *
 * Call board_clock_init() once at the top of main(). Everything that converts
 * a clock frequency into a timer value (005 TIM7, 006 PWM, ...) must ask the
 * board_clock_*_hz() helpers instead of using SystemCoreClock directly.
 */

#ifndef BOARD_CLOCK_H
#define BOARD_CLOCK_H

#include <stdint.h>

/* ------------------------------------------------------------------------- */
/* Target clock tree. Every number below is derived from HSE + PLL + bus      */
/* dividers, so changing one factor updates the whole tree consistently.      */
/* ------------------------------------------------------------------------- */

#define BOARD_HSE_HZ        (8000000U)  /* ST-LINK MCO into PH0, HSE bypass    */
#define BOARD_PLL_M         (4U)        /* 8 MHz / 4 = 2 MHz VCO input         */
#define BOARD_PLL_N         (180U)      /* 2 MHz x 180 = 360 MHz VCO           */
#define BOARD_PLL_P         (2U)        /* 360 MHz / 2 = 180 MHz SYSCLK        */
#define BOARD_PLL_Q         (7U)        /* 360 MHz / 7 = 51.43 MHz             */

#define BOARD_AHB_DIV       (1U)
#define BOARD_APB1_DIV      (4U)
#define BOARD_APB2_DIV      (2U)

#define BOARD_PLL_VCO_IN_HZ (BOARD_HSE_HZ / BOARD_PLL_M)        /* 2 MHz      */
#define BOARD_PLL_VCO_HZ    (BOARD_PLL_VCO_IN_HZ * BOARD_PLL_N) /* 360 MHz    */
#define BOARD_SYSCLK_HZ     (BOARD_PLL_VCO_HZ / BOARD_PLL_P)    /* 180 MHz    */
#define BOARD_HCLK_HZ       (BOARD_SYSCLK_HZ / BOARD_AHB_DIV)   /* 180 MHz    */
#define BOARD_PCLK1_HZ      (BOARD_HCLK_HZ / BOARD_APB1_DIV)    /* 45 MHz     */
#define BOARD_PCLK2_HZ      (BOARD_HCLK_HZ / BOARD_APB2_DIV)    /* 90 MHz     */

/* The x2 rule only applies when the APB prescaler actually divides. */
#define BOARD_APB1_TIMER_HZ \
    ((BOARD_APB1_DIV == 1U) ? BOARD_PCLK1_HZ : (BOARD_PCLK1_HZ * 2U))   /* 90 MHz  */
#define BOARD_APB2_TIMER_HZ \
    ((BOARD_APB2_DIV == 1U) ? BOARD_PCLK2_HZ : (BOARD_PCLK2_HZ * 2U))   /* 180 MHz */

/* Flash wait states required above 150 MHz at VOS scale 1 (RM0390 Flash ACR). */
#define BOARD_FLASH_LATENCY (5U)

/* ------------------------------------------------------------------------- */
/* API                                                                        */
/* ------------------------------------------------------------------------- */

/*
 * Result of board_clock_init(). Any value other than BOARD_CLOCK_OK means the
 * MCU is still running from HSI (16 MHz) and every timing number in the
 * experiment is meaningless. 005 signals this on LD3 and keeps the value in a
 * global so the debugger can read the exact failure step.
 */
typedef enum
{
    BOARD_CLOCK_OK = 0,
    BOARD_CLOCK_ERR_REGULATOR,      /* PWR VOS scale 1 never became ready      */
    BOARD_CLOCK_ERR_FLASH_LATENCY,  /* FLASH_ACR did not accept 5 wait states  */
    BOARD_CLOCK_ERR_HSI_TIMEOUT,    /* HSI never ready                         */
    BOARD_CLOCK_ERR_HSE_TIMEOUT,    /* HSE bypass 8 MHz never ready            */
    BOARD_CLOCK_ERR_PLL_TIMEOUT,    /* PLL never locked                        */
    BOARD_CLOCK_ERR_SYSCLK_SWITCH,  /* SWS never reported the requested source */
    BOARD_CLOCK_ERR_TREE_MISMATCH   /* configuration readback does not match   */
} board_clock_status_t;

/*
 * Program the full clock tree: regulator scale 1, 5 Flash wait states, HSE
 * bypass, PLL 180 MHz, AHB /1, APB1 /4, APB2 /2.
 *
 * Safe to call from any previous state (reset, or a second call): the function
 * first switches SYSCLK back to HSI and stops the PLL, then rebuilds the tree.
 * On failure it returns early; the MCU stays on HSI at 16 MHz with the
 * prescalers still at /1, which is a working (if slow) state.
 *
 * On success SystemCoreClock is refreshed by SystemCoreClockUpdate().
 */
board_clock_status_t board_clock_init(void);

/*
 * Read back the tree that is actually programmed in RCC/FLASH. These helpers
 * are the only supported way to turn a frequency into a timer value: they read
 * the real bus prescalers instead of trusting a constant.
 */

/* HCLK: the Cortex-M4 core clock. This is what SystemCoreClock holds. */
uint32_t board_clock_hclk_hz(void);

/* APB1 / APB2 peripheral clocks (TIM7 counts from BOARD_APB1_TIMER_HZ). */
uint32_t board_clock_pclk1_hz(void);
uint32_t board_clock_pclk2_hz(void);

/* APB1 / APB2 timer kernel clocks, with the x2 rule applied. */
uint32_t board_clock_apb1_timer_hz(void);
uint32_t board_clock_apb2_timer_hz(void);

#endif /* BOARD_CLOCK_H */
