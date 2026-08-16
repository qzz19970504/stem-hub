#include "app_stall_config.h"

#include <stdint.h>

#include "stm32f1xx_hal.h"

static bool App_StallConfigHalUnlock(void *context)
{
    (void)context;
    return HAL_FLASH_Unlock() == HAL_OK;
}

static bool App_StallConfigHalErasePage(void *context,
                                        uint32_t page_address)
{
    FLASH_EraseInitTypeDef erase = {
        .TypeErase = FLASH_TYPEERASE_PAGES,
        .PageAddress = page_address,
        .NbPages = 1U,
    };
    uint32_t page_error = 0U;

    (void)context;
    return HAL_FLASHEx_Erase(&erase, &page_error) == HAL_OK;
}

static bool App_StallConfigHalProgramHalfword(void *context,
                                              uint32_t address,
                                              uint16_t halfword)
{
    (void)context;
    return HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD,
                             address,
                             halfword) == HAL_OK;
}

static bool App_StallConfigHalLock(void *context)
{
    (void)context;
    return HAL_FLASH_Lock() == HAL_OK;
}

static const AppStallConfigRecord *App_StallConfigHalReadRecord(
    void *context,
    uint32_t address)
{
    (void)context;
    return (const AppStallConfigRecord *)(uintptr_t)address;
}

static const AppStallConfigFlashOps g_app_stall_config_flash_operations = {
    .unlock = App_StallConfigHalUnlock,
    .erase_page = App_StallConfigHalErasePage,
    .program_halfword = App_StallConfigHalProgramHalfword,
    .lock = App_StallConfigHalLock,
    .read_record = App_StallConfigHalReadRecord,
    .context = NULL,
};

uint32_t App_StallConfigLoadCurrentMa(void)
{
    return App_StallConfigLoadCurrentMaFromRecord(
        App_StallConfigHalReadRecord(
            NULL,
            APP_STALL_CONFIG_FLASH_PAGE_ADDRESS));
}

bool App_StallConfigStoreCurrentMa(uint32_t current_ma)
{
    return App_StallConfigStoreCurrentMaWithOps(
        current_ma,
        &g_app_stall_config_flash_operations);
}
