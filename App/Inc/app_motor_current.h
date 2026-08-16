#ifndef APP_MOTOR_CURRENT_H
#define APP_MOTOR_CURRENT_H

#include <stdint.h>

uint32_t App_MotorCurrentFromMillivolts(uint32_t millivolts);
uint32_t App_MotorCurrentToDeciAmps(uint32_t current_ma);

#endif
