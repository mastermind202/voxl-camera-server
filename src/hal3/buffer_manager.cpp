/*******************************************************************************************************************************
 *
 * Copyright (c) 2022 ModalAI, Inc.
 *
 ******************************************************************************************************************************/

#include "buffer_manager.h"
#include <modal_journal.h>

#include <condition_variable>
#include <mutex>

using std::unique_lock;
using std::mutex;

static std::mutex bufferMutex;
static std::condition_variable bufferConditionVar;

#define ALIGN_BYTE(x, a) ((x % a == 0) ? x : x - (x % a) + a)

//These two will be implementation-dependent, found in the buffer_impl_*.cpp files
extern int allocateOneBuffer(
        BufferGroup&       bufferGroup,
        unsigned int       index,
        unsigned int       width,
        unsigned int       height,
        unsigned int       format,
        unsigned long int  consumerFlags,
        buffer_handle_t*   pBuffer);

extern void deleteOneBuffer(
        BufferGroup&       bufferGroup,
        unsigned int       index);

//
// =============================================================
//

void bufferDeleteBuffers(BufferGroup& bufferGroup)
{

    if (bufferGroup.totalBuffers != bufferGroup.freeBuffers.size()){
        M_WARN("Deleting buffers: %lu of %d still in use\n",
            (bufferGroup.totalBuffers)-(bufferGroup.freeBuffers.size()),
            bufferGroup.totalBuffers);
    }
    for (unsigned int i = 0; i < bufferGroup.totalBuffers; i++) {
        deleteOneBuffer(bufferGroup, i);
    }
}

int bufferAllocateBuffers(
    BufferGroup& bufferGroup,
    unsigned int totalBuffers,
    unsigned int width,
    unsigned int height,
    unsigned int format,
    unsigned long int consumerFlags)
{
    
    for (uint32_t i = 0; i < totalBuffers; i++) {

        if(allocateOneBuffer(bufferGroup, i, width, height, format, consumerFlags, &bufferGroup.buffers[i])) return -1;

        if(bufferGroup.bufferBlocks[i].vaddress == NULL){
            M_ERROR("Buffer was allocated but did not populate the vaddress field\n");
            return -1;
        }

        bufferGroup.totalBuffers++;
        bufferGroup.freeBuffers.push_back(&bufferGroup.buffers[i]);
    }

    return 0;
}

void bufferPush(BufferGroup& bufferGroup, buffer_handle_t* buffer)
{
    std::unique_lock<mutex> lock(bufferMutex);
    bufferGroup.freeBuffers.push_back(buffer);
    bufferConditionVar.notify_all();
}

void bufferPushAddress(BufferGroup& bufferGroup, void* vaddress)
{
    for (unsigned int i = 0; i < bufferGroup.totalBuffers; i++){
        if (vaddress == bufferGroup.bufferBlocks[i].vaddress){
            bufferPush(bufferGroup, &bufferGroup.buffers[i]);
            return;
        }
    }
    M_ERROR("Recieved invalid buffer in %s\n", __FUNCTION__);
    return;
}

buffer_handle_t* bufferPop(BufferGroup& bufferGroup)
{
    unique_lock<mutex> lock(bufferMutex);
    if (bufferGroup.freeBuffers.size() == 0) {
        bufferConditionVar.wait(lock);
    }

    buffer_handle_t* buffer = bufferGroup.freeBuffers.front();
    bufferGroup.freeBuffers.pop_front();
    return buffer;
}

BufferBlock* bufferGetBufferInfo(BufferGroup* bufferGroup, buffer_handle_t* buffer)
{
    unique_lock<mutex> lock(bufferMutex);
    for (unsigned int i = 0;i < bufferGroup->totalBuffers;i++){
        if (*buffer == bufferGroup->buffers[i]){
            return &(bufferGroup->bufferBlocks[i]);
        }
    }
    M_ERROR("%s wan't able to successfully find the requested buffer\n", __FUNCTION__ );
    return NULL;
}
