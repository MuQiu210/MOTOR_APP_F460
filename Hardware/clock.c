/// @file  clock.c
/// @brief Clock initialization implementation.

#include "clock.h"

#include "main.h"

/// @brief Initialize the application system clock tree.
/// @return None.
void clock_init(void)
{
    stc_clock_xtal_init_t xtal_cfg;
    stc_clock_pll_init_t pll_cfg;

    /* Match the XHCode F460 clock tree: 8 MHz XTAL -> 200 MHz MPLL. */
    CLK_SetClockDiv(CLK_BUS_CLK_ALL,
                    CLK_HCLK_DIV1 | CLK_EXCLK_DIV2 | CLK_PCLK0_DIV1 |
                    CLK_PCLK1_DIV2 | CLK_PCLK2_DIV4 | CLK_PCLK3_DIV4 |
                    CLK_PCLK4_DIV2);
    SRAM_SetWaitCycle(SRAM_SRAM_ALL, SRAM_WAIT_CYCLE1, SRAM_WAIT_CYCLE1);
    SRAM_SetWaitCycle(SRAM_SRAMH, SRAM_WAIT_CYCLE0, SRAM_WAIT_CYCLE0);
    EFM_SetWaitCycle(EFM_WAIT_CYCLE5);

    (void)CLK_XtalStructInit(&xtal_cfg);
    xtal_cfg.u8State = CLK_XTAL_ON;
    xtal_cfg.u8Drv = CLK_XTAL_DRV_HIGH;
    xtal_cfg.u8Mode = CLK_XTAL_MD_OSC;
    xtal_cfg.u8StableTime = CLK_XTAL_STB_2MS;
    (void)CLK_XtalInit(&xtal_cfg);

    (void)CLK_PLLStructInit(&pll_cfg);
    pll_cfg.PLLCFGR = 0UL;
    pll_cfg.PLLCFGR_f.PLLM = 0UL;
    pll_cfg.PLLCFGR_f.PLLN = 49UL;
    pll_cfg.PLLCFGR_f.PLLP = 1UL;
    pll_cfg.PLLCFGR_f.PLLQ = 1UL;
    pll_cfg.PLLCFGR_f.PLLR = 1UL;
    pll_cfg.u8PLLState = CLK_PLL_ON;
    pll_cfg.PLLCFGR_f.PLLSRC = CLK_PLL_SRC_XTAL;
    (void)CLK_PLLInit(&pll_cfg);

    GPIO_SetReadWaitCycle(GPIO_RD_WAIT3);
    PWC_HighSpeedToHighPerformance();
    CLK_SetSysClockSrc(CLK_SYSCLK_SRC_PLL);
}
