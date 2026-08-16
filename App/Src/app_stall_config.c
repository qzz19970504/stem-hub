#include "app_stall_config.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "app_config.h"

#define APP_STALL_CONFIG_MAGIC 0x53544C31U
#define APP_STALL_CONFIG_FORMAT_VERSION 1U
#define APP_STALL_CONFIG_RESERVED_VALUE 0U
#define APP_STALL_CONFIG_CRC_INITIAL 0xFFFFFFFFU
#define APP_STALL_CONFIG_CRC_POLYNOMIAL 0xEDB88320U

_Static_assert(sizeof(AppStallConfigRecord) == 16U,
               "stall configuration record must remain 16 bytes");

static uint32_t App_StallConfigCalculateCrc32(const void *data,
                                              size_t length)
{
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t crc = APP_STALL_CONFIG_CRC_INITIAL;
    size_t byte_index;

    for (byte_index = 0U; byte_index < length; ++byte_index)
    {
        uint32_t bit_index;

        crc ^= bytes[byte_index];
        for (bit_index = 0U; bit_index < 8U; ++bit_index)
        {
            if ((crc & 1U) != 0U)
            {
                crc = (crc >> 1U) ^ APP_STALL_CONFIG_CRC_POLYNOMIAL;
            }
            else
            {
                crc >>= 1U;
            }
        }
    }

    return ~crc;
}

void App_StallConfigBuildRecord(AppStallConfigRecord *record,
                                uint32_t stall_current_ma)
{
    if (record == NULL)
    {
        return;
    }

    record->magic = APP_STALL_CONFIG_MAGIC;
    record->format_version = APP_STALL_CONFIG_FORMAT_VERSION;
    record->reserved = APP_STALL_CONFIG_RESERVED_VALUE;
    record->stall_current_ma = stall_current_ma;
    record->crc32 = App_StallConfigCalculateCrc32(
        record,
        offsetof(AppStallConfigRecord, crc32));
}

bool App_StallConfigRecordIsValid(const AppStallConfigRecord *record)
{
    if (record == NULL)
    {
        return false;
    }

    if ((record->magic != APP_STALL_CONFIG_MAGIC)
        || (record->format_version != APP_STALL_CONFIG_FORMAT_VERSION)
        || (record->reserved != APP_STALL_CONFIG_RESERVED_VALUE)
        || (record->stall_current_ma < APP_MOTOR_STALL_MIN_CURRENT_MA)
        || (record->stall_current_ma > APP_MOTOR_STALL_MAX_CURRENT_MA))
    {
        return false;
    }

    return record->crc32 == App_StallConfigCalculateCrc32(
        record,
        offsetof(AppStallConfigRecord, crc32));
}

uint32_t App_StallConfigLoadCurrentMaFromRecord(
    const AppStallConfigRecord *record)
{
    return App_StallConfigRecordIsValid(record)
        ? record->stall_current_ma
        : APP_MOTOR_STALL_DEFAULT_CURRENT_MA;
}

bool App_StallConfigStoreCurrentMaWithOps(
    uint32_t current_ma,
    const AppStallConfigFlashOps *operations)
{
    AppStallConfigRecord record;
    const uint8_t *record_bytes = (const uint8_t *)&record;
    uint32_t offset;
    bool did_write = true;
    bool did_lock;

    if ((current_ma < APP_MOTOR_STALL_MIN_CURRENT_MA)
        || (current_ma > APP_MOTOR_STALL_MAX_CURRENT_MA)
        || (operations == NULL)
        || (operations->unlock == NULL)
        || (operations->erase_page == NULL)
        || (operations->program_halfword == NULL)
        || (operations->lock == NULL)
        || (operations->read_record == NULL))
    {
        return false;
    }

    App_StallConfigBuildRecord(&record, current_ma);
    if (!operations->unlock(operations->context))
    {
        return false;
    }

    if (!operations->erase_page(operations->context,
                                APP_STALL_CONFIG_FLASH_PAGE_ADDRESS))
    {
        did_write = false;
    }

    for (offset = 0U; did_write && (offset < sizeof(record)); offset += 2U)
    {
        uint16_t halfword;

        (void)memcpy(&halfword, record_bytes + offset, sizeof(halfword));
        did_write = operations->program_halfword(
            operations->context,
            APP_STALL_CONFIG_FLASH_PAGE_ADDRESS + offset,
            halfword);
    }

    did_lock = operations->lock(operations->context);
    if (!did_write || !did_lock)
    {
        return false;
    }

    return App_StallConfigRecordIsValid(
        operations->read_record(operations->context,
                                APP_STALL_CONFIG_FLASH_PAGE_ADDRESS));
}
