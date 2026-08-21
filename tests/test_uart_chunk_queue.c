#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app_uart_chunk_queue.h"

int main(void)
{
    AppUartChunkQueue queue;
    AppUartChunk chunk;
    size_t entry_index;

    assert(AppUartChunkQueue_Init(&queue));
    assert(!AppUartChunkQueue_Pop(&queue, &chunk));
    assert(!AppUartChunkQueue_HasOverflowed(&queue));

    assert(AppUartChunkQueue_Push(&queue, 3U, true, true));
    assert(AppUartChunkQueue_Pop(&queue, &chunk));
    assert(chunk.length == 3U);
    assert(chunk.silence_before);
    assert(chunk.silence_after);

    for (entry_index = 0U;
         entry_index < APP_UART1_CHUNK_QUEUE_CAPACITY;
         ++entry_index)
    {
        assert(AppUartChunkQueue_Push(&queue,
                                      (uint16_t)(entry_index + 1U),
                                      (entry_index % 2U) == 0U,
                                      (entry_index % 3U) == 0U));
    }
    assert(!AppUartChunkQueue_Push(&queue, 99U, false, false));
    assert(AppUartChunkQueue_HasOverflowed(&queue));

    for (entry_index = 0U;
         entry_index < APP_UART1_CHUNK_QUEUE_CAPACITY;
         ++entry_index)
    {
        assert(AppUartChunkQueue_Pop(&queue, &chunk));
        assert(chunk.length == (uint16_t)(entry_index + 1U));
    }
    assert(!AppUartChunkQueue_Pop(&queue, &chunk));

    AppUartChunkQueue_Reset(&queue);
    assert(!AppUartChunkQueue_HasOverflowed(&queue));
    assert(!AppUartChunkQueue_Pop(&queue, &chunk));

    for (entry_index = 0U;
         entry_index < APP_UART1_CHUNK_QUEUE_CAPACITY;
         ++entry_index)
    {
        assert(AppUartChunkQueue_Push(&queue,
                                      (uint16_t)(20U + entry_index),
                                      false,
                                      true));
        assert(AppUartChunkQueue_Pop(&queue, &chunk));
        assert(chunk.length == (uint16_t)(20U + entry_index));
        assert(!chunk.silence_before);
        assert(chunk.silence_after);
    }

    assert(!AppUartChunkQueue_Init(NULL));
    assert(!AppUartChunkQueue_Push(NULL, 1U, false, false));
    assert(!AppUartChunkQueue_Push(&queue, 0U, false, false));
    assert(!AppUartChunkQueue_Pop(NULL, &chunk));
    assert(!AppUartChunkQueue_Pop(&queue, NULL));
    assert(!AppUartChunkQueue_HasOverflowed(NULL));

    return 0;
}
