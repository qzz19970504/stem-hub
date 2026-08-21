#ifndef APP_UART_CHUNK_QUEUE_H
#define APP_UART_CHUNK_QUEUE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app_config.h"

typedef struct
{
    uint16_t length;
    bool silence_before;
    bool silence_after;
} AppUartChunk;

typedef struct
{
    AppUartChunk entries[APP_UART1_CHUNK_QUEUE_CAPACITY];
    volatile size_t head_sequence;
    volatile size_t tail_sequence;
    volatile bool has_overflowed;
} AppUartChunkQueue;

bool AppUartChunkQueue_Init(AppUartChunkQueue *queue);
bool AppUartChunkQueue_Push(AppUartChunkQueue *queue,
                            uint16_t length,
                            bool silence_before,
                            bool silence_after);
bool AppUartChunkQueue_Pop(AppUartChunkQueue *queue, AppUartChunk *chunk);
bool AppUartChunkQueue_HasOverflowed(const AppUartChunkQueue *queue);
void AppUartChunkQueue_Reset(AppUartChunkQueue *queue);

#endif
