/// @file  uart3.c
/// @brief APP UART3 non-blocking implementation (DMA + ring buffer).

#include "uart3.h"
#include "hc32_ll.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "critical.h"
#include "ringbuffer.h"

/* Software FIFO and DMA staging-buffer sizes in bytes. */
#define UART3_RX_FIFO_SIZE (512UL)
#define UART3_TX_FIFO_SIZE (512UL)
/* Keep one full Modbus RTU response inside a single DMA stage when possible. */
#define UART3_TX_DMA_STAGE_SIZE (256UL)
#define UART3_PRINTF_BUFFER_SIZE (128UL)

/* UART3 software FIFOs and the DMA transmit staging buffer. */
static struct ringbuffer s_uart3_rx_fifo;
static struct ringbuffer s_uart3_tx_fifo;
static uint8_t s_uart3_rx_fifo_mem[UART3_RX_FIFO_SIZE];
static uint8_t s_uart3_tx_fifo_mem[UART3_TX_FIFO_SIZE];
static uint8_t s_uart3_tx_dma_stage_mem[UART3_TX_DMA_STAGE_SIZE];

/* `true` while the TX DMA channel is actively sending. */
static volatile bool s_uart3_tx_dma_busy;
/* RX DMA address tracking used by `uart3_recv()` to compute produced bytes. */
static bool s_uart3_rx_dma_addr_inited;
static uint32_t s_uart3_rx_dma_last_addr;

/* F460 shared-INTC callback for DMA1 channel 3 transfer completion. */
void DMA1_TC3_BTC3_Handler(void);

/// @brief Initialize UART3, GPIO, DMA, and the related interrupts.
/// @details RX uses a self-linked DMA LLP descriptor to feed the software FIFO,
/// while TX uses the DMA TC interrupt to continue segmented transfers. The RX
/// DMA address baseline is reset during reinitialization to avoid stale deltas.
/// @param bps UART3 baud rate in bps.
void uart3_init(uint32_t bps)
{
    stc_usart_uart_init_t stc_uart_init;
    stc_dma_init_t stc_dma_rx_init;
    stc_dma_init_t stc_dma_tx_init;
    stc_dma_llp_init_t stc_dma_llp_init;
    uint32_t rx_buf_size;
    static stc_dma_llp_descriptor_t s_uart3_rx_llp;

    (void)ringbuffer_init(&s_uart3_rx_fifo, s_uart3_rx_fifo_mem, sizeof(s_uart3_rx_fifo_mem));
    (void)ringbuffer_init(&s_uart3_tx_fifo, s_uart3_tx_fifo_mem, sizeof(s_uart3_tx_fifo_mem));
    s_uart3_rx_dma_addr_inited = false;
    s_uart3_rx_dma_last_addr = 0UL;

    /* Pin mapping: PH2 -> USART3-RX, PC13 -> USART3-TX. */
    GPIO_SetFunc(GPIO_PORT_H, GPIO_PIN_02, GPIO_FUNC_33);
    GPIO_SetFunc(GPIO_PORT_C, GPIO_PIN_13, GPIO_FUNC_32);

    FCG_Fcg1PeriphClockCmd(FCG1_PERIPH_USART3, ENABLE);

    (void)USART_DeInit(CM_USART3);
    (void)USART_UART_StructInit(&stc_uart_init);
    stc_uart_init.u32ClockSrc = USART_CLK_SRC_INTERNCLK;
    stc_uart_init.u32ClockDiv = USART_CLK_DIV1;
    stc_uart_init.u32CKOutput = USART_CK_OUTPUT_DISABLE;
    stc_uart_init.u32Baudrate = bps;
    stc_uart_init.u32DataWidth = USART_DATA_WIDTH_8BIT;
    stc_uart_init.u32StopBit = USART_STOPBIT_1BIT;
    stc_uart_init.u32Parity = USART_PARITY_NONE;
    stc_uart_init.u32OverSampleBit = USART_OVER_SAMPLE_16BIT;
    stc_uart_init.u32FirstBit = USART_FIRST_BIT_LSB;
    stc_uart_init.u32StartBitPolarity = USART_START_BIT_FALLING;
    /* F460 DDL exposes CTS/RTS names only. RTS selects the DDL's no-CTSE
       path, which is the no-hardware-flow-control behavior used by APP. */
    stc_uart_init.u32HWFlowControl = USART_HW_FLOWCTRL_RTS;
    (void)USART_UART_Init(CM_USART3, &stc_uart_init, NULL);

    USART_FuncCmd(CM_USART3, (USART_TX | USART_RX), ENABLE);

    /* Clear stale USART errors and flush any buffered RX data. */
    USART_ClearStatus(CM_USART3, USART_FLAG_OVERRUN | USART_FLAG_FRAME_ERR | USART_FLAG_PARITY_ERR);
    while (USART_GetStatus(CM_USART3, USART_FLAG_RX_FULL) == SET)
    {
        (void)USART_ReadData(CM_USART3);
    }

    /* Hand RX over to DMA and keep TX_EMPTY enabled for TX DMA triggering. */
    USART_FuncCmd(CM_USART3, USART_INT_RX, DISABLE);
    USART_FuncCmd(CM_USART3, USART_INT_TX_EMPTY, ENABLE);

    /* Preconfigure the UART3 RX DMA path: DMA1_CH2 <- USART3_RDR -> RX FIFO memory. */
    FCG_Fcg0PeriphClockCmd(FCG0_PERIPH_AOS, ENABLE);
    FCG_Fcg0PeriphClockCmd(FCG0_PERIPH_DMA1, ENABLE);

    (void)DMA_ChCmd(CM_DMA1, DMA_CH2, DISABLE);
    (void)DMA_StructInit(&stc_dma_rx_init);
    stc_dma_rx_init.u32IntEn = DMA_INT_DISABLE;
    stc_dma_rx_init.u32SrcAddr = (uint32_t)(&CM_USART3->RDR);
    stc_dma_rx_init.u32DestAddr = (uint32_t)s_uart3_rx_fifo_mem;
    stc_dma_rx_init.u32DataWidth = DMA_DATAWIDTH_8BIT;
    stc_dma_rx_init.u32BlockSize = 1UL;
    rx_buf_size = (uint32_t)sizeof(s_uart3_rx_fifo_mem);
    stc_dma_rx_init.u32TransCount = rx_buf_size;
    stc_dma_rx_init.u32SrcAddrInc = DMA_SRC_ADDR_FIX;
    stc_dma_rx_init.u32DestAddrInc = DMA_DEST_ADDR_INC;
    (void)DMA_Init(CM_DMA1, DMA_CH2, &stc_dma_rx_init);

    /* RX DMA uses a single self-linked LLP descriptor so it wraps automatically. */
    stc_dma_llp_init.u32Mode = DMA_LLP_WAIT;
    stc_dma_llp_init.u32State = DMA_LLP_ENABLE;
    stc_dma_llp_init.u32Addr = (uint32_t)&s_uart3_rx_llp;
    (void)DMA_LlpInit(CM_DMA1, DMA_CH2, &stc_dma_llp_init);

    s_uart3_rx_llp.SARx = stc_dma_rx_init.u32SrcAddr;
    s_uart3_rx_llp.DARx = stc_dma_rx_init.u32DestAddr;
    s_uart3_rx_llp.DTCTLx = ((rx_buf_size << DMA_DTCTL_CNT_POS) |
                             (stc_dma_rx_init.u32BlockSize << DMA_DTCTL_BLKSIZE_POS));
    s_uart3_rx_llp.RPTx = 0UL;
    s_uart3_rx_llp.SNSEQCTLx = 0UL;
    s_uart3_rx_llp.DNSEQCTLx = 0UL;
    s_uart3_rx_llp.LLPx = (uint32_t)&s_uart3_rx_llp;
    s_uart3_rx_llp.CHCTLx = (stc_dma_rx_init.u32SrcAddrInc | stc_dma_rx_init.u32DestAddrInc |
                             stc_dma_rx_init.u32DataWidth | stc_dma_rx_init.u32IntEn |
                             stc_dma_llp_init.u32State | stc_dma_llp_init.u32Mode);

    /* Preconfigure the UART3 TX DMA path: DMA1_CH3 <- TX staging buffer -> USART3_TDR. */
    (void)DMA_ChCmd(CM_DMA1, DMA_CH3, DISABLE);
    (void)DMA_StructInit(&stc_dma_tx_init);
    stc_dma_tx_init.u32IntEn = DMA_INT_ENABLE;
    stc_dma_tx_init.u32SrcAddr = (uint32_t)s_uart3_tx_dma_stage_mem;
    stc_dma_tx_init.u32DestAddr = (uint32_t)(&CM_USART3->TDR);
    stc_dma_tx_init.u32DataWidth = DMA_DATAWIDTH_8BIT;
    stc_dma_tx_init.u32BlockSize = 1UL;
    stc_dma_tx_init.u32TransCount = 1UL;
    stc_dma_tx_init.u32SrcAddrInc = DMA_SRC_ADDR_INC;
    stc_dma_tx_init.u32DestAddrInc = DMA_DEST_ADDR_FIX;
    (void)DMA_Init(CM_DMA1, DMA_CH3, &stc_dma_tx_init);

    DMA_ClearTransCompleteStatus(CM_DMA1, DMA_FLAG_TC_CH3 | DMA_FLAG_BTC_CH3);
    DMA_TransCompleteIntCmd(CM_DMA1, DMA_INT_BTC_CH3, DISABLE);
    DMA_TransCompleteIntCmd(CM_DMA1, DMA_INT_TC_CH3, ENABLE);

    DMA_Cmd(CM_DMA1, ENABLE);
    AOS_SetTriggerEventSrc(AOS_DMA1_2, EVT_SRC_USART3_RI);
    AOS_SetTriggerEventSrc(AOS_DMA1_3, EVT_SRC_USART3_TI);
    (void)DMA_ChCmd(CM_DMA1, DMA_CH2, ENABLE);

    s_uart3_tx_dma_busy = false;

    /* F460 uses INTC sign-in for DMA1 CH3's shared IRQ. */
    stc_irq_signin_config_t stc_irq;
    stc_irq.enIntSrc = INT_SRC_DMA1_TC3;
    stc_irq.enIRQn = INT038_IRQn;
    stc_irq.pfnCallback = &DMA1_TC3_BTC3_Handler;
    (void)INTC_IrqSignIn(&stc_irq);
    NVIC_ClearPendingIRQ(INT038_IRQn);
    NVIC_SetPriority(INT038_IRQn, DDL_IRQ_PRIO_15);
    NVIC_EnableIRQ(INT038_IRQn);
}

/// @brief Queue UART3 data for transmission and start TX DMA when needed.
/// @details The first chunk is copied into the DMA staging buffer immediately
/// when TX DMA is idle. Remaining bytes stay in the software FIFO and are
/// drained from the DMA completion interrupt.
/// @param data Pointer to the bytes to send.
/// @param len Number of bytes to send.
void uart3_send(const void *data, uint32_t len)
{
    uint32_t primask;
    uint32_t out_len;

    if ((data == NULL) || (len == 0UL))
    {
        return;
    }

    primask = critical_enter();
    /* TX DMA uses USART3_TI as the trigger, so TX_EMPTY must remain enabled. */
    // USART_FuncCmd(CM_USART3, USART_INT_TX_EMPTY, ENABLE);
    if (s_uart3_tx_dma_busy)
    {
        out_len = ringbuffer_put(&s_uart3_tx_fifo, data, len);
        (void)out_len;
    }
    else
    {
        uint32_t kick_len = (len > UART3_TX_DMA_STAGE_SIZE) ? UART3_TX_DMA_STAGE_SIZE : len;
        uint32_t remain_len = len - kick_len;

        memcpy(s_uart3_tx_dma_stage_mem, data, kick_len);

        // DMA flags are cleared by the shared DMA1 CH3 callback.
        s_uart3_tx_dma_busy = true;
        (void)DMA_ChCmd(CM_DMA1, DMA_CH3, DISABLE);
        (void)DMA_SetSrcAddr(CM_DMA1, DMA_CH3, (uint32_t)s_uart3_tx_dma_stage_mem);
        (void)DMA_SetTransCount(CM_DMA1, DMA_CH3, (uint16_t)kick_len);
        (void)DMA_ChCmd(CM_DMA1, DMA_CH3, ENABLE);
        DMA_MxChSWTrigger(CM_DMA1, DMA_MX_CH3);

        /* Queue any remaining bytes and let the DMA TC interrupt continue the transfer. */
        if (remain_len > 0UL)
        {
            out_len = ringbuffer_put(&s_uart3_tx_fifo,
                                     (uint8_t *)data + kick_len,
                                     remain_len);
            (void)out_len;
        }
    }
    critical_exit(primask);
}

/// @brief Read bytes from the UART3 RX software FIFO.
/// @details The first call records the RX DMA destination-address baseline and
/// returns 0. Later calls synchronize the ring-buffer write pointer from the DMA
/// address and then pop up to `len` bytes from the FIFO.
/// @param data Output buffer pointer.
/// @param len Maximum number of bytes to read.
/// @retval uint32_t Number of bytes actually read.
uint32_t uart3_recv(void *data, uint32_t len)
{
    uint32_t dma_addr;
    uint32_t err_flags = 0UL;

    /* Clear USART error flags that can otherwise stall RX DMA after cable events. */
    if (USART_GetStatus(CM_USART3, USART_FLAG_OVERRUN) == SET)
    {
        err_flags |= USART_FLAG_OVERRUN;
    }
    if (USART_GetStatus(CM_USART3, USART_FLAG_FRAME_ERR) == SET)
    {
        err_flags |= USART_FLAG_FRAME_ERR;
    }
    if (USART_GetStatus(CM_USART3, USART_FLAG_PARITY_ERR) == SET)
    {
        err_flags |= USART_FLAG_PARITY_ERR;
    }
    if (err_flags != 0UL)
    {
        (void)USART_ReadData(CM_USART3);
        USART_ClearStatus(CM_USART3, err_flags);
    }

    /* Drop the read when RX DMA is disabled and reset the DMA address baseline. */
    if ((CM_DMA1->CHEN & DMA_CHEN_CHEN_2) == 0UL)
    {
        s_uart3_rx_dma_last_addr = 0UL;
        s_uart3_rx_dma_addr_inited = false;
        return 0UL;
    }

    dma_addr = DMA_GetDestAddr(CM_DMA1, DMA_CH2);
    if (!s_uart3_rx_dma_addr_inited)
    {
        s_uart3_rx_dma_last_addr = dma_addr;
        s_uart3_rx_dma_addr_inited = true;
        return 0UL;
    }

    (void)ringbuffer_dma_update(&s_uart3_rx_fifo, dma_addr, s_uart3_rx_dma_last_addr);
    s_uart3_rx_dma_last_addr = dma_addr;
    return ringbuffer_get(&s_uart3_rx_fifo, data, len);
}

/// @brief Format a string and send it through UART3 in a blocking manner.
/// @details This helper is intended for debug printing only and bypasses the
/// TX DMA/FIFO path.
/// @param fmt `printf`-style format string.
/// @param ... Variable arguments consumed by `vsnprintf`.
void uart3_printf(const char *fmt, ...)
{
    char buffer[UART3_PRINTF_BUFFER_SIZE];
    int32_t len;
    va_list args;

    va_start(args, fmt);
    len = vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    /* Return immediately when formatting fails or produces an empty string. */
    if (len <= 0)
    {
        return;
    }
    /* Truncate to the internal buffer size while preserving the terminator. */
    if ((uint32_t)len >= sizeof(buffer))
    {
        len = (int32_t)(sizeof(buffer) - 1UL);
    }

    (void)USART_UART_Trans(CM_USART3,
                           (uint8_t *)buffer,
                           (uint32_t)len,
                           USART_MAX_TIMEOUT);
}


/// @brief Handle the DMA1 CH3 TX-complete interrupt.
/// @details Reload the next TX chunk from the software FIFO or stop the DMA
/// channel when no more bytes are pending.
void DMA1_TC3_BTC3_Handler(void)
{
    uint32_t tx_dma_len;

    DMA_ClearTransCompleteStatus(CM_DMA1, DMA_FLAG_TC_CH3 | DMA_FLAG_BTC_CH3);

    tx_dma_len = ringbuffer_get(&s_uart3_tx_fifo,
                                s_uart3_tx_dma_stage_mem,
                                sizeof(s_uart3_tx_dma_stage_mem));
    if (tx_dma_len == 0UL)
    {
        s_uart3_tx_dma_busy = false;
        (void)DMA_ChCmd(CM_DMA1, DMA_CH3, DISABLE);
        return;
    }

    s_uart3_tx_dma_busy = true;
    (void)DMA_ChCmd(CM_DMA1, DMA_CH3, DISABLE);
    (void)DMA_SetSrcAddr(CM_DMA1, DMA_CH3, (uint32_t)s_uart3_tx_dma_stage_mem);
    (void)DMA_SetTransCount(CM_DMA1, DMA_CH3, (uint16_t)tx_dma_len);
    (void)DMA_ChCmd(CM_DMA1, DMA_CH3, ENABLE);
    /* SW trigger: if TI event already fired before DMA was re-enabled
     * (can happen when this ISR is delayed by higher-priority ADC ISR),
     * the pulse would be missed and the TX chain would silently stall. */
    DMA_MxChSWTrigger(CM_DMA1, DMA_MX_CH3);
}
