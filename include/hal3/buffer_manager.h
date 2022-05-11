/*******************************************************************************************************************************
 *
 * Copyright (c) 2022 ModalAI, Inc.
 *
 ******************************************************************************************************************************/

#ifndef CAMXHAL3BUFFER_H
#define CAMXHAL3BUFFER_H

#include <deque>
#include "hardware/camera3.h"

#define  BUFFER_QUEUE_MAX_SIZE  32

#define ALIGN_BYTE(x, a) ((x % a == 0) ? x : x - (x % a) + a)

typedef struct _BufferBlock {
    void*             vaddress;
    unsigned long int size;
    unsigned int      width;
    unsigned int      height;
    unsigned int      stride;
} BufferBlock;

typedef struct _BufferGroup {
    std::deque<buffer_handle_t*> freeBuffers;
    uint32_t            totalBuffers;
    buffer_handle_t     buffers[BUFFER_QUEUE_MAX_SIZE];
    BufferBlock         bufferBlocks[BUFFER_QUEUE_MAX_SIZE];
} BufferGroup;

int bufferAllocateBuffers(
    BufferGroup *bufferGroup,
    unsigned int totalBuffers,
    unsigned int width,
    unsigned int height,
    unsigned int format,
    unsigned long int consumerFlags);

void bufferDeleteBuffers(BufferGroup* buffer);
void bufferMakeYUVContiguous(BufferBlock* pBufferInfo);
void bufferPush(BufferGroup* bufferGroup, buffer_handle_t* buffer);
buffer_handle_t* bufferPop(BufferGroup* bufferGroup);
BufferBlock* bufferGetBufferInfo(BufferGroup* bufferGroup, buffer_handle_t* buffer);

#endif // CAMXHAL3BUFFER_H
