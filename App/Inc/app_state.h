#ifndef APP_STATE_H
#define APP_STATE_H

#include <stdbool.h>
#include <stdint.h>

#include "app_at_protocol.h"
#include "app_types.h"

void App_StateSetBridgeEnabled(AppBridgeTarget target, bool enabled);
void App_StateGetBridgeEnabled(bool *uart2_enabled, bool *uart3_enabled);

bool App_StateTryGetSensorSnapshot(AppSensorSnapshot *snapshot);
void App_StatePublishSensorSnapshot(AppSensorSnapshot *snapshot, uint32_t tick);

bool App_StateTryGetMotorStatus(AppMotorStatus *status);
void App_StateStoreMotorStatus(AppMotorMode mode,
                               uint32_t current_ma,
                               bool overcurrent_latched,
                               bool drv_fault_active);

void App_StateSetLedMasterEnabled(bool enabled);
bool App_StateTryGetLedAndMotor(bool *led_master_enabled, AppMotorMode *motor_mode);

void App_StateSetOutputEnabled(AppOutputTarget target, bool enabled);

bool App_StateSetChargeOnTimeSeconds(uint32_t seconds);
bool App_StateTryGetChargeOnTimeSeconds(uint32_t *seconds);
bool App_StateSetThermalProtectionActive(bool active);
bool App_StateTryGetThermalProtectionActive(bool *active);

#endif
