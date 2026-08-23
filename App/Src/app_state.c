#include "app_state.h"

#include "app_runtime.h"

typedef struct
{
    AppSensorSnapshot sensor_snapshot;
    AppMotorStatus motor_status;
    AppIoStatus io_status;
    uint32_t charge_on_time_seconds;
    uint32_t stall_current_ma;
    bool bridge_uart2_enabled;
    bool bridge_uart3_enabled;
    bool thermal_protection_active;
} AppState;

static AppState g_app_state = {
    .motor_status = {
        .mode = APP_MOTOR_MODE_SLEEP,
        .current_ma = 0U,
        .overcurrent_latched = false,
        .drv_fault_active = false,
    },
    .io_status = {
        .led_master_enabled = true,
        .nmos1_enabled = false,
        .nmos2_enabled = false,
        .uvlo_enabled = false,
        .mp4317_enabled = false,
        .motor_bypass_enabled = false,
        .charge_bypass_enabled = false,
    },
    .charge_on_time_seconds = APP_CHARGE_DEFAULT_ON_TIME_SECONDS,
    .stall_current_ma = APP_MOTOR_STALL_DEFAULT_CURRENT_MA,
    .bridge_uart2_enabled = false,
    .bridge_uart3_enabled = false,
    .thermal_protection_active = false,
};

void App_StateSetBridgeEnabled(AppBridgeTarget target, bool enabled)
{
    if (osMutexAcquire(g_app_runtime.state_mutex, osWaitForever) != osOK)
    {
        return;
    }

    if ((target == APP_BRIDGE_TARGET_UART2) || (target == APP_BRIDGE_TARGET_UART23))
    {
        g_app_state.bridge_uart2_enabled = enabled;
    }

    if ((target == APP_BRIDGE_TARGET_UART3) || (target == APP_BRIDGE_TARGET_UART23))
    {
        g_app_state.bridge_uart3_enabled = enabled;
    }

    (void)osMutexRelease(g_app_runtime.state_mutex);
}

void App_StateGetBridgeEnabled(bool *uart2_enabled, bool *uart3_enabled)
{
    if ((uart2_enabled == NULL) || (uart3_enabled == NULL))
    {
        return;
    }

    if (osMutexAcquire(g_app_runtime.state_mutex, osWaitForever) != osOK)
    {
        return;
    }

    *uart2_enabled = g_app_state.bridge_uart2_enabled;
    *uart3_enabled = g_app_state.bridge_uart3_enabled;
    (void)osMutexRelease(g_app_runtime.state_mutex);
}

bool App_StateTryGetSensorSnapshot(AppSensorSnapshot *snapshot)
{
    bool ready = false;

    if (snapshot == NULL)
    {
        return false;
    }

    if (osSemaphoreAcquire(g_app_runtime.sensor_ready_semaphore, 0U) == osOK)
    {
        if (osMutexAcquire(g_app_runtime.sensor_mutex, osWaitForever) == osOK)
        {
            *snapshot = g_app_state.sensor_snapshot;
            (void)osMutexRelease(g_app_runtime.sensor_mutex);
            ready = true;
        }

        (void)osSemaphoreRelease(g_app_runtime.sensor_ready_semaphore);
    }

    return ready;
}

void App_StatePublishSensorSnapshot(AppSensorSnapshot *snapshot, uint32_t tick)
{
    if (snapshot == NULL)
    {
        return;
    }

    if (osMutexAcquire(g_app_runtime.sensor_mutex, osWaitForever) == osOK)
    {
        snapshot->sample_tick = tick;
        snapshot->sample_counter = g_app_state.sensor_snapshot.sample_counter + 1U;
        g_app_state.sensor_snapshot = *snapshot;
        (void)osMutexRelease(g_app_runtime.sensor_mutex);

        if (osSemaphoreGetCount(g_app_runtime.sensor_ready_semaphore) == 0U)
        {
            (void)osSemaphoreRelease(g_app_runtime.sensor_ready_semaphore);
        }
    }
}

bool App_StateTryGetMotorStatus(AppMotorStatus *status)
{
    if (status == NULL)
    {
        return false;
    }

    if (osMutexAcquire(g_app_runtime.state_mutex, osWaitForever) != osOK)
    {
        return false;
    }

    *status = g_app_state.motor_status;
    (void)osMutexRelease(g_app_runtime.state_mutex);
    return true;
}

void App_StateStoreMotorStatus(AppMotorMode mode,
                               uint32_t current_ma,
                               bool overcurrent_latched,
                               bool drv_fault_active)
{
    if (osMutexAcquire(g_app_runtime.state_mutex, osWaitForever) == osOK)
    {
        g_app_state.motor_status.mode = mode;
        g_app_state.motor_status.current_ma = current_ma;
        g_app_state.motor_status.overcurrent_latched = overcurrent_latched;
        g_app_state.motor_status.drv_fault_active = drv_fault_active;
        (void)osMutexRelease(g_app_runtime.state_mutex);
    }
}

void App_StateSetLedMasterEnabled(bool enabled)
{
    if (osMutexAcquire(g_app_runtime.state_mutex, osWaitForever) == osOK)
    {
        g_app_state.io_status.led_master_enabled = enabled;
        (void)osMutexRelease(g_app_runtime.state_mutex);
    }
}

bool App_StateTryGetLedAndMotor(bool *led_master_enabled, AppMotorMode *motor_mode)
{
    if ((led_master_enabled == NULL) || (motor_mode == NULL))
    {
        return false;
    }

    if (osMutexAcquire(g_app_runtime.state_mutex, osWaitForever) != osOK)
    {
        return false;
    }

    *led_master_enabled = g_app_state.io_status.led_master_enabled;
    *motor_mode = g_app_state.motor_status.mode;
    (void)osMutexRelease(g_app_runtime.state_mutex);
    return true;
}

void App_StateSetOutputEnabled(AppOutputTarget target, bool enabled)
{
    if (osMutexAcquire(g_app_runtime.state_mutex, osWaitForever) != osOK)
    {
        return;
    }

    switch (target)
    {
    case APP_OUTPUT_TARGET_NMOS1:
        g_app_state.io_status.nmos1_enabled = enabled;
        break;
    case APP_OUTPUT_TARGET_NMOS2:
        g_app_state.io_status.nmos2_enabled = enabled;
        break;
    case APP_OUTPUT_TARGET_UVLO:
        g_app_state.io_status.uvlo_enabled = enabled;
        break;
    case APP_OUTPUT_TARGET_MP4317:
        g_app_state.io_status.mp4317_enabled = enabled;
        break;
    case APP_OUTPUT_TARGET_CHARGE_BYPASS:
        g_app_state.io_status.charge_bypass_enabled = enabled;
        break;
    default:
        break;
    }

    (void)osMutexRelease(g_app_runtime.state_mutex);
}

void App_StateSetMotorBypassEnabled(bool enabled)
{
    if (osMutexAcquire(g_app_runtime.state_mutex, osWaitForever) == osOK)
    {
        g_app_state.io_status.motor_bypass_enabled = enabled;
        (void)osMutexRelease(g_app_runtime.state_mutex);
    }
}

bool App_StateTryGetIoStatus(AppIoStatus *status)
{
    if (status == NULL)
    {
        return false;
    }

    if (osMutexAcquire(g_app_runtime.state_mutex, osWaitForever) != osOK)
    {
        return false;
    }

    *status = g_app_state.io_status;
    (void)osMutexRelease(g_app_runtime.state_mutex);
    return true;
}

bool App_StateSetChargeOnTimeSeconds(uint32_t seconds)
{
    if ((seconds < APP_CHARGE_MIN_ON_TIME_SECONDS)
        || (seconds > APP_CHARGE_MAX_ON_TIME_SECONDS))
    {
        return false;
    }

    if (osMutexAcquire(g_app_runtime.state_mutex, osWaitForever) != osOK)
    {
        return false;
    }

    g_app_state.charge_on_time_seconds = seconds;
    (void)osMutexRelease(g_app_runtime.state_mutex);
    return true;
}

bool App_StateTryGetChargeOnTimeSeconds(uint32_t *seconds)
{
    if (seconds == NULL)
    {
        return false;
    }

    if (osMutexAcquire(g_app_runtime.state_mutex, osWaitForever) != osOK)
    {
        return false;
    }

    *seconds = g_app_state.charge_on_time_seconds;
    (void)osMutexRelease(g_app_runtime.state_mutex);
    return true;
}

bool App_StateSetStallCurrentMa(uint32_t current_ma)
{
    if ((current_ma < APP_MOTOR_STALL_MIN_CURRENT_MA)
        || (current_ma > APP_MOTOR_STALL_MAX_CURRENT_MA))
    {
        return false;
    }

    if (osMutexAcquire(g_app_runtime.state_mutex, osWaitForever) != osOK)
    {
        return false;
    }

    g_app_state.stall_current_ma = current_ma;
    (void)osMutexRelease(g_app_runtime.state_mutex);
    return true;
}

bool App_StateTryGetStallCurrentMa(uint32_t *current_ma)
{
    if (current_ma == NULL)
    {
        return false;
    }

    if (osMutexAcquire(g_app_runtime.state_mutex, osWaitForever) != osOK)
    {
        return false;
    }

    *current_ma = g_app_state.stall_current_ma;
    (void)osMutexRelease(g_app_runtime.state_mutex);
    return true;
}

bool App_StateSetThermalProtectionActive(bool active)
{
    if (osMutexAcquire(g_app_runtime.state_mutex, osWaitForever) != osOK)
    {
        return false;
    }

    g_app_state.thermal_protection_active = active;
    (void)osMutexRelease(g_app_runtime.state_mutex);
    return true;
}

bool App_StateTryGetThermalProtectionActive(bool *active)
{
    if (active == NULL)
    {
        return false;
    }

    if (osMutexAcquire(g_app_runtime.state_mutex, osWaitForever) != osOK)
    {
        return false;
    }

    *active = g_app_state.thermal_protection_active;
    (void)osMutexRelease(g_app_runtime.state_mutex);
    return true;
}
