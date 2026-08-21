#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "app_line_reader.h"

static AppLineReaderStatus push_text(AppLineReader *reader, const char *text)
{
    AppLineReaderStatus status = APP_LINE_READER_NONE;
    size_t character_index;

    for (character_index = 0U; text[character_index] != '\0'; ++character_index)
    {
        status = AppLineReader_Push(reader, (uint8_t)text[character_index]);
    }

    return status;
}

int main(void)
{
    char line[8] = {0};
    AppLineReader reader = {0};
    static const uint8_t binary_line[] = {'X', 0x00U, 'Y', '\r', '\n'};
    size_t byte_index;

    assert(AppLineReader_Init(&reader, line, sizeof(line)));
    assert(push_text(&reader, "AT\r\n") == APP_LINE_READER_COMPLETE);
    assert(strcmp(AppLineReader_GetLine(&reader), "AT\r\n") == 0);

    AppLineReader_Reset(&reader);
    assert(push_text(&reader, "123456\r\n") == APP_LINE_READER_TOO_LONG);
    assert(push_text(&reader, "B\r\n") == APP_LINE_READER_COMPLETE);
    assert(strcmp(AppLineReader_GetLine(&reader), "B\r\n") == 0);

    AppLineReader_Reset(&reader);
    assert(push_text(&reader, "1234567X") == APP_LINE_READER_TOO_LONG);
    assert(push_text(&reader, "junk\r\n") == APP_LINE_READER_NONE);
    assert(push_text(&reader, "C\r\n") == APP_LINE_READER_COMPLETE);

    AppLineReader_Reset(&reader);
    for (byte_index = 0U; byte_index < sizeof(binary_line); ++byte_index)
    {
        assert(AppLineReader_Push(&reader, binary_line[byte_index])
               == ((byte_index + 1U == sizeof(binary_line))
                       ? APP_LINE_READER_COMPLETE
                       : APP_LINE_READER_NONE));
    }
    assert(AppLineReader_GetLineLength(&reader) == sizeof(binary_line));
    assert(memcmp(AppLineReader_GetLine(&reader),
                  binary_line,
                  sizeof(binary_line)) == 0);

    AppLineReader_Reset(&reader);
    assert(AppLineReader_GetLine(&reader) == NULL);
    assert(AppLineReader_GetLineLength(&reader) == 0U);
    assert(!AppLineReader_Init(NULL, line, sizeof(line)));
    assert(!AppLineReader_Init(&reader, NULL, sizeof(line)));
    assert(!AppLineReader_Init(&reader, line, 1U));

    return 0;
}
