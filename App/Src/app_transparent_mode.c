#include "app_transparent_mode.h"

#include <string.h>

static void AppTransparentMode_ClearCandidate(AppTransparentMode *mode)
{
    mode->escape_length = 0U;
    mode->escape_has_pre_guard = false;
    (void)memset(mode->escape_candidate, 0, sizeof(mode->escape_candidate));
}

static void AppTransparentMode_CopyToForward(AppTransparentResult *result,
                                              const uint8_t *bytes,
                                              size_t length)
{
    (void)memcpy(&result->forward[result->forward_length], bytes, length);
    result->forward_length += length;
}

void AppTransparentMode_Init(AppTransparentMode *mode)
{
    if (mode == NULL)
    {
        return;
    }

    (void)memset(mode, 0, sizeof(*mode));
    mode->target = APP_BRIDGE_TARGET_UART2;
}

void AppTransparentMode_Enter(AppTransparentMode *mode, AppBridgeTarget target)
{
    if (mode == NULL)
    {
        return;
    }

    mode->active = true;
    mode->target = target;
    AppTransparentMode_ClearCandidate(mode);
}

void AppTransparentMode_Abort(AppTransparentMode *mode)
{
    if (mode == NULL)
    {
        return;
    }

    mode->active = false;
    mode->target = APP_BRIDGE_TARGET_UART2;
    AppTransparentMode_ClearCandidate(mode);
}

bool AppTransparentMode_IsActive(const AppTransparentMode *mode)
{
    return (mode != NULL) && mode->active;
}

AppBridgeTarget AppTransparentMode_GetTarget(const AppTransparentMode *mode)
{
    return (mode == NULL) ? APP_BRIDGE_TARGET_UART2 : mode->target;
}

bool AppTransparentMode_ProcessChunk(AppTransparentMode *mode,
                                     const uint8_t *bytes,
                                     size_t length,
                                     bool silence_before,
                                     bool silence_after,
                                     AppTransparentResult *result)
{
    size_t byte_index = 0U;

    if ((mode == NULL) || !mode->active || (bytes == NULL)
        || (length == 0U) || (length > APP_UART1_RX_CHUNK_SIZE)
        || (result == NULL))
    {
        return false;
    }

    (void)memset(result, 0, sizeof(*result));

    if ((mode->escape_length == 0U)
        && (!silence_before || (bytes[0] != (uint8_t)'+')))
    {
        AppTransparentMode_CopyToForward(result, bytes, length);
        return true;
    }

    if (mode->escape_length == 0U)
    {
        mode->escape_has_pre_guard = true;
    }

    while ((byte_index < length) && (mode->escape_length < 3U)
           && (bytes[byte_index] == (uint8_t)'+'))
    {
        mode->escape_candidate[mode->escape_length++] = bytes[byte_index++];
    }

    if ((mode->escape_length == 3U) && (byte_index == length)
        && mode->escape_has_pre_guard && silence_after)
    {
        result->exited = true;
        AppTransparentMode_Abort(mode);
        return true;
    }

    if (byte_index < length)
    {
        AppTransparentMode_CopyToForward(result,
                                         mode->escape_candidate,
                                         mode->escape_length);
        AppTransparentMode_CopyToForward(result,
                                         &bytes[byte_index],
                                         length - byte_index);
        AppTransparentMode_ClearCandidate(mode);
    }

    return true;
}
