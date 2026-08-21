#ifndef APP_LINE_READER_H
#define APP_LINE_READER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum
{
    APP_LINE_READER_NONE = 0,
    APP_LINE_READER_COMPLETE,
    APP_LINE_READER_TOO_LONG
} AppLineReaderStatus;

typedef struct
{
    char *buffer;
    size_t capacity;
    size_t length;
    bool is_discarding;
    bool previous_byte_was_carriage_return;
    bool has_complete_line;
} AppLineReader;

bool AppLineReader_Init(AppLineReader *reader, char *buffer, size_t capacity);
AppLineReaderStatus AppLineReader_Push(AppLineReader *reader, uint8_t byte);
const char *AppLineReader_GetLine(const AppLineReader *reader);
size_t AppLineReader_GetLineLength(const AppLineReader *reader);
void AppLineReader_Reset(AppLineReader *reader);

#endif
