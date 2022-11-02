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

// Platform Specific Flags
#ifdef APQ8096
    #define HAL3_FMT_YUV  HAL_PIXEL_FORMAT_YCbCr_420_888
#elif QRB5165
    #define HAL3_FMT_YUV  HAL_PIXEL_FORMAT_YCBCR_420_888
#else
    #error "No Platform defined"
#endif

typedef struct _BufferBlock {
    void*             vaddress;
    unsigned long int size;
    unsigned int      width;
    unsigned int      height;
    unsigned int      stride;
    unsigned int      slice;
} BufferBlock;

typedef struct _BufferGroup {
    std::deque<buffer_handle_t*> freeBuffers;
    uint32_t            totalBuffers;
    buffer_handle_t     buffers[BUFFER_QUEUE_MAX_SIZE];
    BufferBlock         bufferBlocks[BUFFER_QUEUE_MAX_SIZE];
} BufferGroup;

int bufferAllocateBuffers(
    BufferGroup& bufferGroup,
    unsigned int totalBuffers,
    unsigned int width,
    unsigned int height,
    unsigned int format,
    unsigned long int consumerFlags);

void bufferDeleteBuffers(BufferGroup& buffer);
void bufferMakeYUVContiguous(BufferBlock* pBufferInfo);
void bufferPush(BufferGroup& bufferGroup, buffer_handle_t* buffer);
void bufferPushAddress(BufferGroup& bufferGroup, void* vaddress);
buffer_handle_t* bufferPop(BufferGroup& bufferGroup);
BufferBlock* bufferGetBufferInfo(BufferGroup* bufferGroup, buffer_handle_t* buffer);

#endif // CAMXHAL3BUFFER_H
