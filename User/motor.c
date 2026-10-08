/// @file  motor.c
/// @brief Motor ISR driver for overcurrent protection and M4310 stepping.
/// `adc1_handler()` runs at priority 00 / 20 kHz for overcurrent handling.
/// `pend_sv_handler()` runs at priority 03 / 1 kHz for the main M4310 step.

#include "motor.h"
#include "Model.h"
#include "gpio.h"
#include "save.h"

/* GPIO timing probe mode: 0 = disabled, 1 = ISR scope, 2 = main-loop scope. */
#define MOTOR_PROBE_MODE 0

#if MOTOR_PROBE_MODE == 1
#define PROBE_ISR_ENTER() GPIO_SetPins(GPIO_PORT_C, GPIO_PIN_14)
#define PROBE_ISR_EXIT() GPIO_ResetPins(GPIO_PORT_C, GPIO_PIN_14)
#define PROBE_MAIN_ENTER()
#define PROBE_MAIN_EXIT()
#elif MOTOR_PROBE_MODE == 2
#define PROBE_ISR_ENTER()
#define PROBE_ISR_EXIT()
#define PROBE_MAIN_ENTER() GPIO_SetPins(GPIO_PORT_C, GPIO_PIN_14)
#define PROBE_MAIN_EXIT() GPIO_ResetPins(GPIO_PORT_C, GPIO_PIN_14)
#else
#define PROBE_ISR_ENTER()
#define PROBE_ISR_EXIT()
#define PROBE_MAIN_ENTER()
#define PROBE_MAIN_EXIT()
#endif

SaveInfo *Save = &g_save.model_save;

static void motor_status_led_update(void)
{
    static bool s_stop_mode = true;
    bool stop_mode = (Model_Y.Info.Mode == StopMode);

    if (stop_mode != s_stop_mode)
    {
        gpio_status_led_set_stop(stop_mode);
        s_stop_mode = stop_mode;
    }
    if (stop_mode)
    {
        Model_U.Command.Mode = StopMode;
    }
}

/// @brief Handle the ADC1 interrupt and run the fast motor-control step.
__RAM_FUNC void ADC1_Handler(void)
{
    PROBE_ISR_ENTER();
    ADC_ClearStatus(CM_ADC1, ADC_FLAG_EOCA);

    static volatile uint8_t s_motor_count = 0U;

    /* AWD0 hardware overcurrent disables PWM output and reports the fault to the M4310. */
    if (ADC_AWD_GetStatus(CM_ADC1, ADC_AWD_FLAG_CH0) == SET ||
        ADC_AWD_GetStatus(CM_ADC2, ADC_AWD_FLAG_CH0) == SET ||
        ADC_AWD_GetStatus(CM_ADC2, ADC_AWD_FLAG_CH3) == SET)
    {
        ADC_AWD_ClearStatus(CM_ADC1, ADC_AWD_FLAG_CH0);
        ADC_AWD_ClearStatus(CM_ADC2, ADC_AWD_FLAG_CH0);
        ADC_AWD_ClearStatus(CM_ADC2, ADC_AWD_FLAG_CH3);
        Model_U.Motor.SetFault |= HardOverCurrent;
    }

    if (!encoder_recv(&Model_U.Motor.Angle))
    {
        Model_U.Motor.SetFault |= MotorSensorLoss;
    }

    Model_U.Motor.HallAdcValue[0] = HALL1_ADC;
    Model_U.Motor.HallAdcValue[1] = HALL2_ADC;
    Model_U.Motor.CurrentAdcValue[0] = IU_ADC;
    Model_U.Motor.CurrentAdcValue[1] = IV_ADC;
    Model_U.Motor.CurrentAdcValue[2] = IW_ADC;
    Model_U.Motor.VoltageAdcValue = PWR_24V_DET;
    Model_U.Motor.MosTempAdcValue = MOS_TEMP;
    Model_U.Motor.MotorTempAdcValue = MOTOR_TEMP;

    InterruptUpdate();

    Model_U.Motor.SetFault = NoFault;

    /* Keep PWM always enabled. When disabled, set duty to 0 for braking. */
    if (Model_Y.Enable)
    {
        /* Normal operation: use calculated duty cycles */
        PWMOUT_U = (uint16_t)((Model_Y.Duty[0] * PWMOUT_MAX));
        PWMOUT_V = (uint16_t)((Model_Y.Duty[1] * PWMOUT_MAX));
        PWMOUT_W = (uint16_t)((Model_Y.Duty[2] * PWMOUT_MAX));
    }
    else
    {
        /* Braking: upper MOSFETs 0%, lower MOSFETs 100% (complementary) */
        PWMOUT_U = 0U;
        PWMOUT_V = 0U;
        PWMOUT_W = 0U;
    }

    if (++s_motor_count >= 10)
    {
        s_motor_count = 0;
        SCB->ICSR = SCB_ICSR_PENDSVSET_Msk;
    }

    PROBE_ISR_EXIT();
}

/// @brief Handle the PendSV interrupt and run the slow motor-control step.
__RAM_FUNC void PendSV_Handler(void)
{
    MainUpdate();
    motor_status_led_update();
    Model_U.Command.ResetFault = NoFault;
    if (Model_Y.SaveRequest)
    {
        save_request();
    }
}


/// @brief Initialize the motor-control runtime defaults.
void motor_init(void)
{
    ModelInit();
    NVIC_SetPriority(PendSV_IRQn, DDL_IRQ_PRIO_03);
    /* Enable PWM output once during initialization */
    PWMOUT_ENABLE();
}
