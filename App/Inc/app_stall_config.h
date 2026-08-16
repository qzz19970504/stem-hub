#ifndef APP_STALL_CONFIG_H
#define APP_STALL_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

#define APP_STALL_CONFIG_FLASH_PAGE_ADDRESS 0x0800FC00U

typedef struct
{
    uint32_t magic;
    uint16_t format_version;
    uint16_t reserved;
    uint32_t stall_current_ma;
    uint32_t crc32;
} AppStallConfigRecord;

typedef struct
{
    bool (*unlock)(void *context);
    bool (*erase_page)(void *context, uint32_t page_address);
    bool (*program_halfword)(void *context,
                             uint32_t address,
                             uint16_t halfword);
    bool (*lock)(void *context);
    const AppStallConfigRecord *(*read_record)(void *context,
                                               uint32_t address);
    void *context;
} AppStallConfigFlashOps;

void App_StallConfigBuildRecord(AppStallConfigRecord *record,
                                uint32_t stall_current_ma);
bool App_StallConfigRecordIsValid(const AppStallConfigRecord *record);
uint32_t App_StallConfigLoadCurrentMaFromRecord(
    const AppStallConfigRecord *record);
bool App_StallConfigStoreCurrentMaWithOps(
    uint32_t current_ma,
    const AppStallConfigFlashOps *operations);

uint32_t App_StallConfigLoadCurrentMa(void);
bool App_StallConfigStoreCurrentMa(uint32_t current_ma);

#endif
