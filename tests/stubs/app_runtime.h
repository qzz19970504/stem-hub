#ifndef TEST_APP_RUNTIME_H
#define TEST_APP_RUNTIME_H

#include "cmsis_os.h"

typedef struct
{
    osSemaphoreId_t sensor_ready_semaphore;
    osMutexId_t bridge_mutex;
    osMutexId_t sensor_mutex;
    osMutexId_t state_mutex;
} AppRuntime;

extern AppRuntime g_app_runtime;

#endif
