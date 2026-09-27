/*
 *	stm32h5_hal_stub.c — STM32H533RE クロック/ボード初期化 + HAL スタブ
 *
 *	STM32CubeMX を使わず直接レジスタ操作でクロックを設定する。
 *
 *	STM32H533RET6 クロックツリー (RM0481 Section 11):
 *	  HSI = 64MHz (リセット時は HSIDIV=/2 で 32MHz なので /1 に戻す)
 *	  PLL1: HSI/16 * 125 / 2 = 250MHz SYSCLK
 *	    PLL1M = 16 → VCO input = 64/16 = 4MHz (PLL1RGE = 4-8MHz)
 *	    PLL1N = 125 → VCO = 4 * 125 = 500MHz (wide VCO: 192-836MHz)
 *	    PLL1P = 2  → PLL1P = 500/2 = 250MHz (SYSCLK)
 *
 *	  HCLK = SYSCLK / 1 = 250MHz
 *	  APB1 = HCLK / 1 = 250MHz (TIM2/TIM3, USART2, I2C1)
 *	  APB2 = HCLK / 1 = 250MHz (SPI1, USB)
 *
 *	  USB:    HSI48 (CRS で USB SOF に同期)
 *	  SPI1:   250MHz / 32 = 7.8125MHz (< W5500 max 80MHz)
 *	  USART2: ST-LINK 仮想 COM (PA2=TX, PA3=RX, AF7)
 */

#include <stdint.h>

/*======================================================================
 * レジスタ定義 (RM0481, CMSIS stm32h533xx.h)
 *====================================================================*/

#define REG(a)          (*(volatile uint32_t *)(a))

/* RCC */
#define RCC_BASE        0x44020C00UL
#define RCC_CR          REG(RCC_BASE + 0x0000)
#define RCC_CFGR1       REG(RCC_BASE + 0x001C)
#define RCC_PLL1CFGR    REG(RCC_BASE + 0x0028)
#define RCC_PLL1DIVR    REG(RCC_BASE + 0x0034)
#define RCC_AHB2ENR     REG(RCC_BASE + 0x008C)
#define RCC_APB1LENR    REG(RCC_BASE + 0x009C)

#define RCC_CR_HSION        (1u << 0)
#define RCC_CR_HSIRDY       (1u << 1)
#define RCC_CR_HSIDIV_MASK  (3u << 3)
#define RCC_CR_HSIDIVF      (1u << 5)
#define RCC_CR_PLL1ON       (1u << 24)
#define RCC_CR_PLL1RDY      (1u << 25)

/* RCC_PLL1CFGR bits */
#define PLL1CFGR_PLL1SRC_HSI    (1u << 0)   /* 01 = hsi_ck */
#define PLL1CFGR_PLL1RGE_4_8    (2u << 2)   /* VCO input 4-8MHz */
#define PLL1CFGR_PLL1VCOSEL_WIDE (0u << 5)  /* Wide VCO */
#define PLL1CFGR_PLL1M(m)       ((uint32_t)(m) << 8)
#define PLL1CFGR_PLL1PEN        (1u << 16)

/* RCC_PLL1DIVR bits (各分周値 - 1 を書く) */
#define PLL1DIVR_N(n)   ((uint32_t)((n) - 1) << 0)
#define PLL1DIVR_P(p)   ((uint32_t)((p) - 1) << 9)
#define PLL1DIVR_Q(q)   ((uint32_t)((q) - 1) << 16)
#define PLL1DIVR_R(r)   ((uint32_t)((r) - 1) << 24)

/* RCC_CFGR1 bits */
#define CFGR1_SW_MASK    (3u << 0)
#define CFGR1_SW_PLL1    (3u << 0)
#define CFGR1_SWS_MASK   (3u << 3)
#define CFGR1_SWS_PLL1   (3u << 3)

/* RCC clock enable bits */
#define RCC_AHB2ENR_GPIOAEN     (1u << 0)
#define RCC_AHB2ENR_GPIOBEN     (1u << 1)
#define RCC_AHB2ENR_GPIOCEN     (1u << 2)
#define RCC_APB1LENR_USART2EN   (1u << 17)

/* PWR (電圧スケーリング) */
#define PWR_BASE        0x44020800UL
#define PWR_VOSCR       REG(PWR_BASE + 0x0010)
#define PWR_VOSSR       REG(PWR_BASE + 0x0014)
#define PWR_VOSCR_VOS0  (3u << 4)   /* VOS0: 最高性能 (250MHz対応) */
#define PWR_VOSSR_VOSRDY (1u << 3)  /* VOS ready */

/* Flash ACR */
#define FLASH_ACR       REG(0x40022000UL)
#define FLASH_ACR_LATENCY_MASK    (0xFu << 0)
#define FLASH_ACR_WRHIGHFREQ_MASK (3u << 4)

/* Instruction cache */
#define ICACHE_CR       REG(0x40030400UL)
#define ICACHE_CR_EN    (1u << 0)

/* GPIOA (USART2 ピン設定) */
#define GPIOA_BASE      0x42020000UL
#define GPIOA_MODER     REG(GPIOA_BASE + 0x00)
#define GPIOA_PUPDR     REG(GPIOA_BASE + 0x0C)
#define GPIOA_AFRL      REG(GPIOA_BASE + 0x20)

/*======================================================================
 * SystemCoreClock
 *====================================================================*/
uint32_t SystemCoreClock = 32000000;

/*======================================================================
 * SystemClock_Config — HSI 64MHz → PLL1 250MHz
 *
 * RM0481 Section 11.4 PLL1 設定手順:
 *   1. HSI を 64MHz (HSIDIV=/1) にする
 *   2. PWR 電圧スケーリングを VOS0 に設定 (250MHz に必要)
 *   3. Flash ウェイトステート設定 (250MHz @ VOS0 → 5WS, WRHIGHFREQ=2)
 *   4. PLL1 設定 (PLL1M=16, PLL1N=125, PLL1P=2)
 *   5. PLL1 有効化
 *   6. SYSCLK を PLL1 に切り替え
 *====================================================================*/
static void SystemClock_Config(void)
{
    /* 1. HSI 64MHz */
    RCC_CR |= RCC_CR_HSION;
    while (!(RCC_CR & RCC_CR_HSIRDY)) {}
    RCC_CR &= ~RCC_CR_HSIDIV_MASK;
    while (!(RCC_CR & RCC_CR_HSIDIVF)) {}

    /* 2. PWR 電圧スケーリング VOS0 (250MHz に必要) */
    PWR_VOSCR = PWR_VOSCR_VOS0;
    while (!(PWR_VOSSR & PWR_VOSSR_VOSRDY)) {}

    /* 3. Flash ウェイトステート: 250MHz @ VOS0 → 5WS, WRHIGHFREQ=2 (RM0481 Table 45) */
    FLASH_ACR = (FLASH_ACR & ~(FLASH_ACR_LATENCY_MASK | FLASH_ACR_WRHIGHFREQ_MASK))
              | (5u << 0) | (2u << 4);
    while ((FLASH_ACR & FLASH_ACR_LATENCY_MASK) != 5u) {}

    /* 4. PLL1 設定 (PLL1 OFF の状態で設定) */
    RCC_CR &= ~RCC_CR_PLL1ON;
    while (RCC_CR & RCC_CR_PLL1RDY) {}

    RCC_PLL1CFGR = PLL1CFGR_PLL1SRC_HSI | PLL1CFGR_PLL1RGE_4_8 |
                   PLL1CFGR_PLL1VCOSEL_WIDE | PLL1CFGR_PLL1M(16) |
                   PLL1CFGR_PLL1PEN;

    RCC_PLL1DIVR = PLL1DIVR_N(125) | PLL1DIVR_P(2) |
                   PLL1DIVR_Q(2) | PLL1DIVR_R(2);

    /* 5. PLL1 有効化 */
    RCC_CR |= RCC_CR_PLL1ON;
    while (!(RCC_CR & RCC_CR_PLL1RDY)) {}

    /* 6. SYSCLK を PLL1 に切り替え */
    RCC_CFGR1 = (RCC_CFGR1 & ~CFGR1_SW_MASK) | CFGR1_SW_PLL1;
    while ((RCC_CFGR1 & CFGR1_SWS_MASK) != CFGR1_SWS_PLL1) {}

    SystemCoreClock = 250000000;

    /* 命令キャッシュ有効化 (5WS の Flash 実行を補う) */
    ICACHE_CR |= ICACHE_CR_EN;
}

/*======================================================================
 * Board_Init — T-Monitor コンソール (USART2) のピンとクロック
 *
 * NUCLEO-H533RE の ST-LINK 仮想 COM は USART2 (PA2=TX, PA3=RX, AF7)。
 * 通信設定 (ボーレート) は tm_com_init() が行う。
 *====================================================================*/
static void Board_Init(void)
{
    RCC_AHB2ENR |= RCC_AHB2ENR_GPIOAEN | RCC_AHB2ENR_GPIOBEN | RCC_AHB2ENR_GPIOCEN;
    RCC_APB1LENR |= RCC_APB1LENR_USART2EN;
    (void)RCC_APB1LENR;

    /* PA2/PA3: AF7 (USART2), RX はプルアップ */
    GPIOA_AFRL  = (GPIOA_AFRL & ~((0xFu << 8) | (0xFu << 12))) | (7u << 8) | (7u << 12);
    GPIOA_PUPDR = (GPIOA_PUPDR & ~(3u << 6)) | (1u << 6);
    GPIOA_MODER = (GPIOA_MODER & ~((3u << 4) | (3u << 6))) | (2u << 4) | (2u << 6);
}

/*======================================================================
 * HAL 互換関数 (halif.h が宣言)
 *====================================================================*/
uint32_t halif_get_pclk1(void)         { return SystemCoreClock; }
uint32_t halif_get_pclk2(void)         { return SystemCoreClock; }
uint32_t HAL_RCC_GetPCLK2Freq(void)    { return SystemCoreClock; }
uint32_t HAL_RCC_GetPCLK1Freq(void)    { return SystemCoreClock; }
uint32_t HAL_RCC_GetHCLKFreq(void)     { return SystemCoreClock; }
uint32_t HAL_RCC_GetSysClockFreq(void) { return SystemCoreClock; }
uint32_t HAL_RCC_GetCpuClockFreq(void) { return SystemCoreClock; }

/*======================================================================
 * main() — Reset_Handler → main() → knl_start_mtkernel()
 *====================================================================*/
extern void knl_start_mtkernel(void);

int main(void)
{
    SystemClock_Config();
    Board_Init();
    knl_start_mtkernel();
    while (1) {}
    return 0;
}
