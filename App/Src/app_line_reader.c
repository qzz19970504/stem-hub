#include "app_line_reader.h"

bool AppLineReader_Init(AppLineReader *reader, char *buffer, size_t capacity)
{
    if ((reader == NULL) || (buffer == NULL) || (capacity < 2U))
    {
        return false;
    }

    reader->buffer = buffer;
    reader->capacity = capacity;
    AppLineReader_Reset(reader);
    return true;
}

AppLineReaderStatus AppLineReader_Push(AppLineReader *reader, uint8_t byte)
{
    bool line_ended;

    if ((reader == NULL) || (reader->buffer == NULL) || reader->has_complete_line)
    {
        return APP_LINE_READER_NONE;
    }

    if (reader->is_discarding)
    {
        line_ended = reader->previous_byte_was_carriage_return && (byte == '\n');
        reader->previous_byte_was_carriage_return = (byte == '\r');
        if (line_ended)
        {
            reader->is_discarding = false;
            reader->previous_byte_was_carriage_return = false;
        }
        return APP_LINE_READER_NONE;
    }

    if ((reader->length + 1U) >= reader->capacity)
    {
        line_ended = (byte == '\n')
            && (reader->length > 0U)
            && (reader->buffer[reader->length - 1U] == '\r');
        reader->length = 0U;
        reader->buffer[0] = '\0';
        reader->is_discarding = !line_ended;
        reader->previous_byte_was_carriage_return = !line_ended && (byte == '\r');
        return APP_LINE_READER_TOO_LONG;
    }

    reader->buffer[reader->length++] = (char)byte;
    line_ended = (reader->length >= 2U)
        && (reader->buffer[reader->length - 2U] == '\r')
        && (reader->buffer[reader->length - 1U] == '\n');
    if (!line_ended)
    {
        return APP_LINE_READER_NONE;
    }

    reader->buffer[reader->length] = '\0';
    reader->has_complete_line = true;
    return APP_LINE_READER_COMPLETE;
}

const char *AppLineReader_GetLine(const AppLineReader *reader)
{
    if ((reader == NULL) || !reader->has_complete_line)
    {
        return NULL;
    }

    return reader->buffer;
}

size_t AppLineReader_GetLineLength(const AppLineReader *reader)
{
    if ((reader == NULL) || !reader->has_complete_line)
    {
        return 0U;
    }

    return reader->length;
}

void AppLineReader_Reset(AppLineReader *reader)
{
    if ((reader == NULL) || (reader->buffer == NULL))
    {
        return;
    }

    reader->length = 0U;
    reader->is_discarding = false;
    reader->previous_byte_was_carriage_return = false;
    reader->has_complete_line = false;
    reader->buffer[0] = '\0';
}
