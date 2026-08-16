#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "app_stall_config.h"

typedef struct
{
    AppStallConfigRecord flash_record;
    bool should_unlock;
    bool should_erase;
    bool should_program;
    bool should_lock;
    bool should_return_written_record;
    uint32_t unlock_count;
    uint32_t erase_count;
    uint32_t program_count;
    uint32_t lock_count;
} FakeFlash;

static bool FakeUnlock(void *context)
{
    FakeFlash *flash = (FakeFlash *)context;

    flash->unlock_count++;
    return flash->should_unlock;
}

static bool FakeErasePage(void *context, uint32_t page_address)
{
    FakeFlash *flash = (FakeFlash *)context;

    assert(page_address == APP_STALL_CONFIG_FLASH_PAGE_ADDRESS);
    flash->erase_count++;
    (void)memset(&flash->flash_record, 0xFF, sizeof(flash->flash_record));
    return flash->should_erase;
}

static bool FakeProgramHalfword(void *context,
                                uint32_t address,
                                uint16_t halfword)
{
    FakeFlash *flash = (FakeFlash *)context;
    uint32_t offset = address - APP_STALL_CONFIG_FLASH_PAGE_ADDRESS;

    assert(offset < sizeof(flash->flash_record));
    flash->program_count++;
    if (!flash->should_program)
    {
        return false;
    }

    (void)memcpy(((uint8_t *)&flash->flash_record) + offset,
                 &halfword,
                 sizeof(halfword));
    return true;
}

static bool FakeLock(void *context)
{
    FakeFlash *flash = (FakeFlash *)context;

    flash->lock_count++;
    return flash->should_lock;
}

static const AppStallConfigRecord *FakeReadRecord(void *context,
                                                  uint32_t address)
{
    FakeFlash *flash = (FakeFlash *)context;

    assert(address == APP_STALL_CONFIG_FLASH_PAGE_ADDRESS);
    return flash->should_return_written_record ? &flash->flash_record : NULL;
}

static AppStallConfigFlashOps MakeFlashOps(FakeFlash *flash)
{
    AppStallConfigFlashOps operations = {
        .unlock = FakeUnlock,
        .erase_page = FakeErasePage,
        .program_halfword = FakeProgramHalfword,
        .lock = FakeLock,
        .read_record = FakeReadRecord,
        .context = flash,
    };

    return operations;
}

static FakeFlash MakeWorkingFlash(void)
{
    FakeFlash flash = {
        .should_unlock = true,
        .should_erase = true,
        .should_program = true,
        .should_lock = true,
        .should_return_written_record = true,
    };

    return flash;
}

static AppStallConfigRecord MakeDefaultRecord(void)
{
    AppStallConfigRecord record;

    App_StallConfigBuildRecord(&record, 4000U);
    return record;
}

static void TestDefaultRecordHasStableFormatAndCrc(void)
{
    AppStallConfigRecord record = MakeDefaultRecord();

    assert(sizeof(record) == 16U);
    assert(record.magic == 0x53544C31U);
    assert(record.format_version == 1U);
    assert(record.reserved == 0U);
    assert(record.stall_current_ma == 4000U);
    assert(record.crc32 == 0x59B6320BU);
    assert(App_StallConfigRecordIsValid(&record));
}

static void TestEveryProtectedFieldIsValidated(void)
{
    AppStallConfigRecord record = MakeDefaultRecord();

    record.magic ^= 1U;
    assert(!App_StallConfigRecordIsValid(&record));

    record = MakeDefaultRecord();
    record.format_version++;
    assert(!App_StallConfigRecordIsValid(&record));

    record = MakeDefaultRecord();
    record.reserved = 1U;
    assert(!App_StallConfigRecordIsValid(&record));

    record = MakeDefaultRecord();
    record.stall_current_ma = 999U;
    assert(!App_StallConfigRecordIsValid(&record));

    record = MakeDefaultRecord();
    record.stall_current_ma = 30001U;
    assert(!App_StallConfigRecordIsValid(&record));

    record = MakeDefaultRecord();
    record.crc32 ^= 1U;
    assert(!App_StallConfigRecordIsValid(&record));
}

static void TestErasedAndNullRecordsAreInvalid(void)
{
    AppStallConfigRecord erased_record;

    (void)memset(&erased_record, 0xFF, sizeof(erased_record));
    assert(!App_StallConfigRecordIsValid(&erased_record));
    assert(!App_StallConfigRecordIsValid(NULL));
}

static void TestLoadUsesValidRecordOrDefault(void)
{
    AppStallConfigRecord record;

    App_StallConfigBuildRecord(&record, 4200U);
    assert(App_StallConfigLoadCurrentMaFromRecord(&record) == 4200U);

    record.crc32 ^= 1U;
    assert(App_StallConfigLoadCurrentMaFromRecord(&record) == 4000U);
    assert(App_StallConfigLoadCurrentMaFromRecord(NULL) == 4000U);
}

static void TestStoreErasesProgramsLocksAndVerifies(void)
{
    FakeFlash flash = MakeWorkingFlash();
    AppStallConfigFlashOps operations = MakeFlashOps(&flash);

    assert(App_StallConfigStoreCurrentMaWithOps(4200U, &operations));
    assert(flash.unlock_count == 1U);
    assert(flash.erase_count == 1U);
    assert(flash.program_count == 8U);
    assert(flash.lock_count == 1U);
    assert(App_StallConfigRecordIsValid(&flash.flash_record));
    assert(flash.flash_record.stall_current_ma == 4200U);
}

static void TestStoreRejectsInvalidInputWithoutUnlocking(void)
{
    FakeFlash flash = MakeWorkingFlash();
    AppStallConfigFlashOps operations = MakeFlashOps(&flash);

    assert(!App_StallConfigStoreCurrentMaWithOps(999U, &operations));
    assert(!App_StallConfigStoreCurrentMaWithOps(30001U, &operations));
    assert(!App_StallConfigStoreCurrentMaWithOps(4000U, NULL));
    assert(flash.unlock_count == 0U);
}

static void TestStoreLocksAfterEraseProgramAndVerifyFailures(void)
{
    FakeFlash flash = MakeWorkingFlash();
    AppStallConfigFlashOps operations = MakeFlashOps(&flash);

    flash.should_erase = false;
    assert(!App_StallConfigStoreCurrentMaWithOps(4200U, &operations));
    assert(flash.lock_count == 1U);

    flash = MakeWorkingFlash();
    operations = MakeFlashOps(&flash);
    flash.should_program = false;
    assert(!App_StallConfigStoreCurrentMaWithOps(4200U, &operations));
    assert(flash.lock_count == 1U);

    flash = MakeWorkingFlash();
    operations = MakeFlashOps(&flash);
    flash.should_return_written_record = false;
    assert(!App_StallConfigStoreCurrentMaWithOps(4200U, &operations));
    assert(flash.lock_count == 1U);

    flash = MakeWorkingFlash();
    operations = MakeFlashOps(&flash);
    flash.should_lock = false;
    assert(!App_StallConfigStoreCurrentMaWithOps(4200U, &operations));
    assert(flash.lock_count == 1U);
}

int main(void)
{
    TestDefaultRecordHasStableFormatAndCrc();
    TestEveryProtectedFieldIsValidated();
    TestErasedAndNullRecordsAreInvalid();
    TestLoadUsesValidRecordOrDefault();
    TestStoreErasesProgramsLocksAndVerifies();
    TestStoreRejectsInvalidInputWithoutUnlocking();
    TestStoreLocksAfterEraseProgramAndVerifyFailures();
    puts("OK: stall configuration record and CRC verified.");
    return 0;
}
