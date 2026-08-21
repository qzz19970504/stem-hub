#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "app_transparent_mode.h"

static AppTransparentResult process(AppTransparentMode *mode,
                                    const uint8_t *bytes,
                                    size_t length,
                                    bool silence_before,
                                    bool silence_after)
{
    AppTransparentResult result = {0};

    assert(AppTransparentMode_ProcessChunk(mode,
                                           bytes,
                                           length,
                                           silence_before,
                                           silence_after,
                                           &result));
    return result;
}

static void expect_forward(AppTransparentMode *mode,
                           const uint8_t *bytes,
                           size_t length,
                           bool silence_before,
                           bool silence_after,
                           const uint8_t *expected,
                           size_t expected_length)
{
    const AppTransparentResult result = process(mode,
                                                bytes,
                                                length,
                                                silence_before,
                                                silence_after);

    assert(!result.exited);
    assert(result.forward_length == expected_length);
    assert(memcmp(result.forward, expected, expected_length) == 0);
}

int main(void)
{
    static const uint8_t escape[] = {'+', '+', '+'};
    static const uint8_t plus[] = {'+'};
    static const uint8_t next_byte[] = {'X'};
    static const uint8_t replayed_escape[] = {'+', '+', '+', 'X'};
    static const uint8_t at_payload[] = "AT+MOTOR=STOP\r\n";
    static const uint8_t embedded_escape[] = "abc+++def";
    static const uint8_t four_pluses[] = "++++";
    static const uint8_t failed_candidate[] = {'+', '+', 'X'};
    static const uint8_t binary_payload[] = {0x00U, 0xFFU, 0x0DU, 0x0AU};
    AppTransparentMode mode;
    AppTransparentResult result;

    AppTransparentMode_Init(&mode);
    assert(!AppTransparentMode_IsActive(&mode));

    AppTransparentMode_Enter(&mode, APP_BRIDGE_TARGET_UART2);
    assert(AppTransparentMode_IsActive(&mode));
    assert(AppTransparentMode_GetTarget(&mode) == APP_BRIDGE_TARGET_UART2);
    expect_forward(&mode,
                   at_payload,
                   sizeof(at_payload) - 1U,
                   true,
                   true,
                   at_payload,
                   sizeof(at_payload) - 1U);

    result = process(&mode, escape, sizeof(escape), true, true);
    assert(result.exited);
    assert(result.forward_length == 0U);
    assert(!AppTransparentMode_IsActive(&mode));

    AppTransparentMode_Enter(&mode, APP_BRIDGE_TARGET_UART3);
    assert(AppTransparentMode_GetTarget(&mode) == APP_BRIDGE_TARGET_UART3);
    expect_forward(&mode,
                   escape,
                   sizeof(escape),
                   false,
                   true,
                   escape,
                   sizeof(escape));

    AppTransparentMode_Enter(&mode, APP_BRIDGE_TARGET_UART23);
    assert(AppTransparentMode_GetTarget(&mode) == APP_BRIDGE_TARGET_UART23);
    result = process(&mode, escape, sizeof(escape), true, false);
    assert(!result.exited);
    assert(result.forward_length == 0U);
    expect_forward(&mode,
                   next_byte,
                   sizeof(next_byte),
                   false,
                   true,
                   replayed_escape,
                   sizeof(replayed_escape));

    expect_forward(&mode,
                   embedded_escape,
                   sizeof(embedded_escape) - 1U,
                   true,
                   true,
                   embedded_escape,
                   sizeof(embedded_escape) - 1U);
    expect_forward(&mode,
                   four_pluses,
                   sizeof(four_pluses) - 1U,
                   true,
                   true,
                   four_pluses,
                   sizeof(four_pluses) - 1U);
    expect_forward(&mode,
                   failed_candidate,
                   sizeof(failed_candidate),
                   true,
                   true,
                   failed_candidate,
                   sizeof(failed_candidate));
    expect_forward(&mode,
                   binary_payload,
                   sizeof(binary_payload),
                   true,
                   true,
                   binary_payload,
                   sizeof(binary_payload));

    result = process(&mode, plus, sizeof(plus), true, false);
    assert(!result.exited);
    assert(result.forward_length == 0U);
    result = process(&mode, plus, sizeof(plus), false, false);
    assert(!result.exited);
    assert(result.forward_length == 0U);
    result = process(&mode, plus, sizeof(plus), false, true);
    assert(result.exited);
    assert(result.forward_length == 0U);
    assert(!AppTransparentMode_IsActive(&mode));

    AppTransparentMode_Enter(&mode, APP_BRIDGE_TARGET_UART2);
    result = process(&mode, plus, sizeof(plus), true, false);
    assert(result.forward_length == 0U);
    AppTransparentMode_Abort(&mode);
    assert(!AppTransparentMode_IsActive(&mode));

    assert(!AppTransparentMode_ProcessChunk(NULL,
                                            escape,
                                            sizeof(escape),
                                            true,
                                            true,
                                            &result));
    assert(!AppTransparentMode_ProcessChunk(&mode,
                                            NULL,
                                            sizeof(escape),
                                            true,
                                            true,
                                            &result));
    assert(!AppTransparentMode_ProcessChunk(&mode,
                                            escape,
                                            0U,
                                            true,
                                            true,
                                            &result));
    assert(!AppTransparentMode_ProcessChunk(&mode,
                                            escape,
                                            APP_UART1_RX_CHUNK_SIZE + 1U,
                                            true,
                                            true,
                                            &result));
    assert(!AppTransparentMode_ProcessChunk(&mode,
                                            escape,
                                            sizeof(escape),
                                            true,
                                            true,
                                            NULL));

    return 0;
}
