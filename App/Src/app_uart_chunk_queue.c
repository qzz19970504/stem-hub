#include "app_uart_chunk_queue.h"

bool AppUartChunkQueue_Init(AppUartChunkQueue *queue)
{
    if (queue == NULL)
    {
        return false;
    }

    queue->head_sequence = 0U;
    queue->tail_sequence = 0U;
    queue->has_overflowed = false;
    return true;
}

bool AppUartChunkQueue_Push(AppUartChunkQueue *queue,
                            uint16_t length,
                            bool silence_before,
                            bool silence_after)
{
    size_t head_sequence;
    AppUartChunk *chunk;

    if ((queue == NULL) || (length == 0U))
    {
        return false;
    }

    head_sequence = queue->head_sequence;
    if ((head_sequence - queue->tail_sequence) >= APP_UART1_CHUNK_QUEUE_CAPACITY)
    {
        queue->has_overflowed = true;
        return false;
    }

    chunk = &queue->entries[head_sequence % APP_UART1_CHUNK_QUEUE_CAPACITY];
    chunk->length = length;
    chunk->silence_before = silence_before;
    chunk->silence_after = silence_after;
    queue->head_sequence = head_sequence + 1U;
    return true;
}

bool AppUartChunkQueue_Pop(AppUartChunkQueue *queue, AppUartChunk *chunk)
{
    size_t tail_sequence;

    if ((queue == NULL) || (chunk == NULL))
    {
        return false;
    }

    tail_sequence = queue->tail_sequence;
    if (tail_sequence == queue->head_sequence)
    {
        return false;
    }

    *chunk = queue->entries[tail_sequence % APP_UART1_CHUNK_QUEUE_CAPACITY];
    queue->tail_sequence = tail_sequence + 1U;
    return true;
}

bool AppUartChunkQueue_HasOverflowed(const AppUartChunkQueue *queue)
{
    return (queue != NULL) && queue->has_overflowed;
}

void AppUartChunkQueue_Reset(AppUartChunkQueue *queue)
{
    if (queue == NULL)
    {
        return;
    }

    queue->tail_sequence = queue->head_sequence;
    queue->has_overflowed = false;
}
