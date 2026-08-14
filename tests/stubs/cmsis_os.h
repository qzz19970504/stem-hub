#ifndef TEST_CMSIS_OS_H
#define TEST_CMSIS_OS_H

#include <stdint.h>

typedef void *osMutexId_t;
typedef void *osSemaphoreId_t;

typedef enum
{
    osOK = 0,
    osError = -1
} osStatus_t;

#define osWaitForever UINT32_MAX

osStatus_t osMutexAcquire(osMutexId_t mutex_id, uint32_t timeout);
osStatus_t osMutexRelease(osMutexId_t mutex_id);
osStatus_t osSemaphoreAcquire(osSemaphoreId_t semaphore_id, uint32_t timeout);
osStatus_t osSemaphoreRelease(osSemaphoreId_t semaphore_id);
uint32_t osSemaphoreGetCount(osSemaphoreId_t semaphore_id);

#endif
