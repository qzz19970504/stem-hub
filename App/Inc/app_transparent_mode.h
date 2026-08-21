#ifndef APP_TRANSPARENT_MODE_H
#define APP_TRANSPARENT_MODE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app_at_protocol.h"
#include "app_config.h"

typedef struct
{
    bool active;
    AppBridgeTarget target;
    uint8_t escape_candidate[3];
    size_t escape_length;
    bool escape_has_pre_guard;
} AppTransparentMode;

typedef struct
{
    uint8_t forward[APP_UART1_RX_CHUNK_SIZE + 3U];
    size_t forward_length;
    bool exited;
} AppTransparentResult;

void AppTransparentMode_Init(AppTransparentMode *mode);
void AppTransparentMode_Enter(AppTransparentMode *mode, AppBridgeTarget target);
void AppTransparentMode_Abort(AppTransparentMode *mode);
bool AppTransparentMode_IsActive(const AppTransparentMode *mode);
AppBridgeTarget AppTransparentMode_GetTarget(const AppTransparentMode *mode);
bool AppTransparentMode_ProcessChunk(AppTransparentMode *mode,
                                     const uint8_t *bytes,
                                     size_t length,
                                     bool silence_before,
                                     bool silence_after,
                                     AppTransparentResult *result);

#endif
