/// @file  encoder.c
/// @brief KTH7112 magnetic encoder driver.
/// Architecture:
/// The TMR4_1 valley interrupt (CNT=0) pulls CS low, resets DMA, and
/// starts one 4-byte SPI transfer. DMA auto-stops after 4 bytes without
/// a dedicated DMA ISR. Later, the ADC1 ISR calls `encoder_recv()`,
/// which checks the DMA TC flag, releases CS, validates CRC, and returns
/// the latest angle.
/// SPI wiring (F460 XHCode configuration):
/// PA0  (MOSI) -> KTH7112 PIN2 (SDA), bidirectional data
/// PA12 (MISO) -> shared SDA input path
/// PA15 (SCK)  -> KTH7112 PIN6 (SCLK)
/// PB6 (GPIO) -> KTH7112 PIN3 (CS), software-controlled

#include "encoder.h"
#include "hc32_ll.h"

/* Hardware mapping. */
#define ENC_CS_PORT GPIO_PORT_B
#define ENC_CS_PIN GPIO_PIN_06

#define ENC_SPI CM_SPI1
#define ENC_DMA CM_DMA2
#define ENC_DMA_RX_CH DMA_CH0
#define ENC_DMA_TX_CH DMA_CH1

/* Protocol constants. */
#define ENC_FRAME_LEN 4U /* 1 command + 2 angle bytes + 1 CRC byte */
#define ENC_CMD_READ_ANGLE 0x00U

/* Static transfer buffers. */
static const uint8_t tx_buf[ENC_FRAME_LEN] = {
    ENC_CMD_READ_ANGLE, 0xFFU, 0xFFU, 0xFFU};
static uint8_t rx_buf[ENC_FRAME_LEN];

/* CRC-8 / ITU lookup table: poly=0x07, init=0x00, xor-out=0x55. */
static const uint8_t crc8_itu_table[256] = {
    0x00U, 0x07U, 0x0EU, 0x09U, 0x1CU, 0x1BU, 0x12U, 0x15U,
    0x38U, 0x3FU, 0x36U, 0x31U, 0x24U, 0x23U, 0x2AU, 0x2DU,
    0x70U, 0x77U, 0x7EU, 0x79U, 0x6CU, 0x6BU, 0x62U, 0x65U,
    0x48U, 0x4FU, 0x46U, 0x41U, 0x54U, 0x53U, 0x5AU, 0x5DU,
    0xE0U, 0xE7U, 0xEEU, 0xE9U, 0xFCU, 0xFBU, 0xF2U, 0xF5U,
    0xD8U, 0xDFU, 0xD6U, 0xD1U, 0xC4U, 0xC3U, 0xCAU, 0xCDU,
    0x90U, 0x97U, 0x9EU, 0x99U, 0x8CU, 0x8BU, 0x82U, 0x85U,
    0xA8U, 0xAFU, 0xA6U, 0xA1U, 0xB4U, 0xB3U, 0xBAU, 0xBDU,
    0xC7U, 0xC0U, 0xC9U, 0xCEU, 0xDBU, 0xDCU, 0xD5U, 0xD2U,
    0xFFU, 0xF8U, 0xF1U, 0xF6U, 0xE3U, 0xE4U, 0xEDU, 0xEAU,
    0xB7U, 0xB0U, 0xB9U, 0xBEU, 0xABU, 0xACU, 0xA5U, 0xA2U,
    0x8FU, 0x88U, 0x81U, 0x86U, 0x93U, 0x94U, 0x9DU, 0x9AU,
    0x27U, 0x20U, 0x29U, 0x2EU, 0x3BU, 0x3CU, 0x35U, 0x32U,
    0x1FU, 0x18U, 0x11U, 0x16U, 0x03U, 0x04U, 0x0DU, 0x0AU,
    0x57U, 0x50U, 0x59U, 0x5EU, 0x4BU, 0x4CU, 0x45U, 0x42U,
    0x6FU, 0x68U, 0x61U, 0x66U, 0x73U, 0x74U, 0x7DU, 0x7AU,
    0x89U, 0x8EU, 0x87U, 0x80U, 0x95U, 0x92U, 0x9BU, 0x9CU,
    0xB1U, 0xB6U, 0xBFU, 0xB8U, 0xADU, 0xAAU, 0xA3U, 0xA4U,
    0xF9U, 0xFEU, 0xF7U, 0xF0U, 0xE5U, 0xE2U, 0xEBU, 0xECU,
    0xC1U, 0xC6U, 0xCFU, 0xC8U, 0xDDU, 0xDAU, 0xD3U, 0xD4U,
    0x69U, 0x6EU, 0x67U, 0x60U, 0x75U, 0x72U, 0x7BU, 0x7CU,
    0x51U, 0x56U, 0x5FU, 0x58U, 0x4DU, 0x4AU, 0x43U, 0x44U,
    0x19U, 0x1EU, 0x17U, 0x10U, 0x05U, 0x02U, 0x0BU, 0x0CU,
    0x21U, 0x26U, 0x2FU, 0x28U, 0x3DU, 0x3AU, 0x33U, 0x34U,
    0x4EU, 0x49U, 0x40U, 0x47U, 0x52U, 0x55U, 0x5CU, 0x5BU,
    0x76U, 0x71U, 0x78U, 0x7FU, 0x6AU, 0x6DU, 0x64U, 0x63U,
    0x3EU, 0x39U, 0x30U, 0x37U, 0x22U, 0x25U, 0x2CU, 0x2BU,
    0x06U, 0x01U, 0x08U, 0x0FU, 0x1AU, 0x1DU, 0x14U, 0x13U,
    0xAEU, 0xA9U, 0xA0U, 0xA7U, 0xB2U, 0xB5U, 0xBCU, 0xBBU,
    0x96U, 0x91U, 0x98U, 0x9FU, 0x8AU, 0x8DU, 0x84U, 0x83U,
    0xDEU, 0xD9U, 0xD0U, 0xD7U, 0xC2U, 0xC5U, 0xCCU, 0xCBU,
    0xE6U, 0xE1U, 0xE8U, 0xEFU, 0xFAU, 0xFDU, 0xF4U, 0xF3U};


/// @brief Compute the CRC-8/ITU checksum for an encoder payload.
static uint8_t crc8_itu(const uint8_t *data, uint8_t len)
{
    uint8_t crc = 0x00U;
    for (uint8_t i = 0U; i < len; i++)
    {
        crc = crc8_itu_table[crc ^ data[i]];
    }
    return crc ^ 0x55U;
}

/// @brief Start one encoder SPI frame using DMA.
static void encoder_start_frame(void)
{
    GPIO_ResetPins(ENC_CS_PORT, ENC_CS_PIN);

    /* Flush stale RX data and clear SPI error flags. */
    (void)SPI_ReadData(ENC_SPI);
    SPI_ClearStatus(ENC_SPI, SPI_FLAG_CLR_ALL);

    /* Clear the DMA transfer-complete flag so `encoder_recv()` can detect completion. */
    DMA_ClearTransCompleteStatus(ENC_DMA, DMA_FLAG_TC_CH0);

    /* RX DMA: 4 bytes from SPI_DR to `rx_buf`. */
    DMA_SetDestAddr(ENC_DMA, ENC_DMA_RX_CH, (uint32_t)rx_buf);
    DMA_SetTransCount(ENC_DMA, ENC_DMA_RX_CH, ENC_FRAME_LEN);
    DMA_ChCmd(ENC_DMA, ENC_DMA_RX_CH, ENABLE);

    /* TX DMA: the remaining 3 bytes from `tx_buf[1..3]` to SPI_DR. */
    DMA_SetSrcAddr(ENC_DMA, ENC_DMA_TX_CH, (uint32_t)&tx_buf[1]);
    DMA_SetTransCount(ENC_DMA, ENC_DMA_TX_CH, ENC_FRAME_LEN - 1U);
    DMA_ChCmd(ENC_DMA, ENC_DMA_TX_CH, ENABLE);

    /* Kick-start the transfer with the first byte; DMA sends the rest. */
    SPI_WriteData(ENC_SPI, tx_buf[0]);
}

/// @brief Handle the TMR4_1 valley interrupt and start the next encoder frame.
void TMR4_1_Ovf_Udf_Handler(void)
{
    bool valley_pending = (TMR4_GetStatus(CM_TMR4_1, TMR4_FLAG_CNT_VALLEY) == SET);

    /* Peak and valley share this vector, so clear both flags together. */
    TMR4_ClearStatus(CM_TMR4_1, TMR4_FLAG_CNT_PEAK | TMR4_FLAG_CNT_VALLEY);

    if (valley_pending)
    {
        encoder_start_frame();
    }
}

/// @brief Initialize the encoder SPI, DMA, and trigger path.
void encoder_init(void)
{
    stc_spi_init_t spi_cfg;
    stc_dma_init_t dma_cfg;
    stc_gpio_init_t cs_gpio;

    /* 1) Enable peripheral clocks. */
    FCG_Fcg1PeriphClockCmd(FCG1_PERIPH_SPI1, ENABLE);
    FCG_Fcg0PeriphClockCmd(FCG0_PERIPH_DMA2 | FCG0_PERIPH_AOS, ENABLE);

    /* 2) Configure SPI1 alternate-function GPIOs. */
    GPIO_SetFunc(GPIO_PORT_A, GPIO_PIN_00, GPIO_FUNC_40); /* MOSI */
    GPIO_SetFunc(GPIO_PORT_A, GPIO_PIN_12, GPIO_FUNC_41); /* MISO */
    GPIO_SetFunc(GPIO_PORT_A, GPIO_PIN_15, GPIO_FUNC_43); /* SCK  */

    /* Configure CS as a push-pull GPIO output and leave it idle high. */
    (void)GPIO_StructInit(&cs_gpio);
    cs_gpio.u16PinDir = PIN_DIR_OUT;
    cs_gpio.u16PinAttr = PIN_ATTR_DIGITAL;
    (void)GPIO_Init(ENC_CS_PORT, ENC_CS_PIN, &cs_gpio);
    GPIO_SetPins(ENC_CS_PORT, ENC_CS_PIN);

    /* 3) Configure SPI1 as an 8-bit mode-3 master at 6.25 MHz. */
    (void)SPI_StructInit(&spi_cfg);
    spi_cfg.u32WireMode = SPI_3_WIRE;
    spi_cfg.u32TransMode = SPI_FULL_DUPLEX;
    spi_cfg.u32MasterSlave = SPI_MASTER;
    spi_cfg.u32Parity = SPI_PARITY_INVD;
    spi_cfg.u32SpiMode = SPI_MD_3;
    spi_cfg.u32BaudRatePrescaler = SPI_BR_CLK_DIV16;
    spi_cfg.u32DataBits = SPI_DATA_SIZE_8BIT;
    spi_cfg.u32FirstBit = SPI_FIRST_MSB;
    spi_cfg.u32SuspendMode = SPI_COM_SUSP_FUNC_OFF;
    spi_cfg.u32FrameLevel = SPI_1_FRAME;
    (void)SPI_Init(ENC_SPI, &spi_cfg);
    {
        stc_spi_delay_t spi_delay;

        /* Keep the XHCode timing defaults explicit: one SCK for each gap. */
        (void)SPI_DelayStructInit(&spi_delay);
        (void)SPI_DelayTimeConfig(ENC_SPI, &spi_delay);
    }
    SPI_SetLoopbackMode(ENC_SPI, SPI_LOOPBACK_INVD);
    SPI_ParityCheckCmd(ENC_SPI, DISABLE);
    SPI_Cmd(ENC_SPI, ENABLE);

    /* 4) Enable DMA2 globally. */
    DMA_Cmd(ENC_DMA, ENABLE);

    /* 5) Configure DMA2 CH0 for SPI RX: SPI_DR -> rx_buf. */
    (void)DMA_StructInit(&dma_cfg);
    dma_cfg.u32IntEn = DMA_INT_DISABLE;
    dma_cfg.u32BlockSize = 1U;
    dma_cfg.u32TransCount = ENC_FRAME_LEN;
    dma_cfg.u32DataWidth = DMA_DATAWIDTH_8BIT;
    dma_cfg.u32SrcAddrInc = DMA_SRC_ADDR_FIX;
    dma_cfg.u32DestAddrInc = DMA_DEST_ADDR_INC;
    dma_cfg.u32SrcAddr = (uint32_t)&ENC_SPI->DR;
    dma_cfg.u32DestAddr = (uint32_t)rx_buf;
    (void)DMA_Init(ENC_DMA, ENC_DMA_RX_CH, &dma_cfg);
    AOS_SetTriggerEventSrc(AOS_DMA2_0, EVT_SRC_SPI1_SPRI);

    /* 6) Configure DMA2 CH1 for SPI TX: tx_buf -> SPI_DR. */
    (void)DMA_StructInit(&dma_cfg);
    dma_cfg.u32IntEn = DMA_INT_DISABLE;
    dma_cfg.u32BlockSize = 1U;
    dma_cfg.u32TransCount = ENC_FRAME_LEN - 1U;
    dma_cfg.u32DataWidth = DMA_DATAWIDTH_8BIT;
    dma_cfg.u32SrcAddrInc = DMA_SRC_ADDR_INC;
    dma_cfg.u32DestAddrInc = DMA_DEST_ADDR_FIX;
    dma_cfg.u32SrcAddr = (uint32_t)&tx_buf[1];
    dma_cfg.u32DestAddr = (uint32_t)&ENC_SPI->DR;
    (void)DMA_Init(ENC_DMA, ENC_DMA_TX_CH, &dma_cfg);
    AOS_SetTriggerEventSrc(AOS_DMA2_1, EVT_SRC_SPI1_SPTI);

    /* 7) Clear stale DMA flags. No DMA interrupt is required. */
    DMA_ClearTransCompleteStatus(ENC_DMA, DMA_FLAG_TC_CH0 | DMA_FLAG_BTC_CH0);

    /* 8) Enable the TMR4_1 valley interrupt that schedules encoder transfers. */
    TMR4_ClearStatus(CM_TMR4_1, TMR4_FLAG_CNT_PEAK | TMR4_FLAG_CNT_VALLEY);
    TMR4_IntCmd(CM_TMR4_1, TMR4_INT_CNT_VALLEY, ENABLE);
    /* F460 routes TMR4 sources through the shared INTC before NVIC. */
    stc_irq_signin_config_t stc_irq;
    stc_irq.enIntSrc = INT_SRC_TMR4_1_UDF;
    stc_irq.enIRQn = INT092_IRQn;
    stc_irq.pfnCallback = &TMR4_1_Ovf_Udf_Handler;
    (void)INTC_IrqSignIn(&stc_irq);
    NVIC_ClearPendingIRQ(INT092_IRQn);
    NVIC_SetPriority(INT092_IRQn, DDL_IRQ_PRIO_02);
    NVIC_EnableIRQ(INT092_IRQn);
}

#define ENC_MAX_CONSECUTIVE_FAILS 3U

/// @brief Read the most recent encoder angle if the SPI DMA transfer has completed.
bool encoder_recv(int32_t *angle)
{
    static uint8_t fail_count = 0U;

    if (DMA_GetTransCompleteStatus(ENC_DMA, DMA_FLAG_TC_CH0) == RESET)
    {
        return (fail_count < ENC_MAX_CONSECUTIVE_FAILS);
    }

    /* The transfer is complete, so release CS and validate the payload. */
    GPIO_SetPins(ENC_CS_PORT, ENC_CS_PIN);
    DMA_ClearTransCompleteStatus(ENC_DMA, DMA_FLAG_TC_CH0);

    if (crc8_itu(&rx_buf[1], 2U) != rx_buf[3])
    {
        /* Keep the previous angle and count consecutive CRC failures. */
        if (fail_count < ENC_MAX_CONSECUTIVE_FAILS)
        {
            fail_count++;
        }
        return (fail_count < ENC_MAX_CONSECUTIVE_FAILS);
    }

    /* Convert the 16-bit raw angle into the fixed-point format expected by the model. */
    fail_count = 0U;
    *angle = (int32_t)((((uint32_t)rx_buf[1] << 8) | rx_buf[2]) << 16);
    return true;
}
