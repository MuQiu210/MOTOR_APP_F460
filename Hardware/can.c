/// @file  can.c
/// @brief F460 classical CAN peripheral initialization and message interface.

#include "can.h"

#include <string.h>

#include "hc32_ll.h"
#include "main.h"

#define APP_CAN CM_CAN

/// @brief Initialize the F460 classical CAN peripheral.
void can1_init(void)
{
    stc_can_init_t can_init_cfg;

    /* Pin mapping from the F460 XHCode configuration. */
    GPIO_SetFunc(GPIO_PORT_B, GPIO_PIN_12, GPIO_FUNC_50); /* CAN-TX */
    GPIO_SetFunc(GPIO_PORT_B, GPIO_PIN_03, GPIO_FUNC_51); /* CAN-RX */

    (void)CAN_StructInit(&can_init_cfg);
    can_init_cfg.stcBitCfg.u32Prescaler = 1U;
    can_init_cfg.stcBitCfg.u32TimeSeg1 = 6U;
    can_init_cfg.stcBitCfg.u32TimeSeg2 = 2U;
    can_init_cfg.stcBitCfg.u32SJW = 2U;
    can_init_cfg.u8WorkMode = CAN_WORK_MD_NORMAL;
    can_init_cfg.u8PTBSingleShotTx = CAN_PTB_SINGLESHOT_TX_DISABLE;
    can_init_cfg.u8STBSingleShotTx = CAN_STB_SINGLESHOT_TX_DISABLE;
    can_init_cfg.u8STBPrioMode = CAN_STB_PRIO_MD_DISABLE;
    can_init_cfg.u8RxWarnLimit = 1U;
    can_init_cfg.u8ErrorWarnLimit = 11U;
    can_init_cfg.u8RxAllFrame = CAN_RX_ALL_FRAME_DISABLE;
    can_init_cfg.u8RxOvfMode = CAN_RX_OVF_SAVE_NEW;

    FCG_Fcg1PeriphClockCmd(FCG1_PERIPH_CAN, ENABLE);
    (void)CAN_Init(APP_CAN, &can_init_cfg);

    /* Keep the generated peripheral interrupt flags compatible with XHCode.
       APP receives frames by polling, so no CAN source is signed into NVIC. */
    CAN_IntCmd(APP_CAN, CAN_INT_ALL, DISABLE);
    CAN_IntCmd(APP_CAN,
               CAN_INT_RX | CAN_INT_RX_OVERRUN | CAN_INT_RX_BUF_FULL |
                   CAN_INT_RX_BUF_WARN | CAN_INT_PTB_TX | CAN_INT_STB_TX |
                   CAN_INT_ERR_INT,
               ENABLE);
}

/// @brief Queue one classic CAN frame in the primary transmit buffer.
bool can1_send(const can_msg_t *msg)
{
    stc_can_tx_frame_t tx;
    uint8_t dlc;

    if (msg == NULL)
    {
        return false;
    }

    dlc = (msg->dlc > 8U) ? 8U : msg->dlc;
    (void)memset(&tx, 0, sizeof(tx));
    tx.u32ID = msg->id;
    tx.IDE = (msg->ide != 0U) ? 1U : 0U;
    tx.RTR = (msg->rtr != 0U) ? 1U : 0U;
    tx.DLC = dlc;
    tx.FDF = 0U;
    tx.BRS = 0U;
    (void)memcpy(tx.au8Data, msg->data, sizeof(tx.au8Data));

    if (CAN_FillTxFrame(APP_CAN, CAN_TX_BUF_PTB, &tx) != LL_OK)
    {
        return false;
    }

    CAN_StartTx(APP_CAN, CAN_TX_REQ_PTB);
    return true;
}

/// @brief Read one frame from the classical CAN receive buffer.
bool can1_recv(can_msg_t *msg)
{
    stc_can_rx_frame_t rx;
    uint8_t copy_len;

    if (msg == NULL || CAN_GetRxBufStatus(APP_CAN) == CAN_RX_BUF_EMPTY)
    {
        return false;
    }

    if (CAN_GetRxFrame(APP_CAN, &rx) != LL_OK)
    {
        return false;
    }

    copy_len = (rx.DLC > 8U) ? 8U : (uint8_t)rx.DLC;
    msg->id = rx.u32ID;
    msg->ide = (uint8_t)rx.IDE;
    msg->rtr = (uint8_t)rx.RTR;
    msg->dlc = copy_len;
    (void)memset(msg->data, 0, sizeof(msg->data));
    (void)memcpy(msg->data, rx.au8Data, copy_len);
    return true;
}
