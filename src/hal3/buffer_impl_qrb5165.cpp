/*******************************************************************************************************************************
 *
 * Copyright (c) 2022 ModalAI, Inc.
 *
 ******************************************************************************************************************************/

#ifdef QRB5165

#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/ion.h>
#include <linux/msm_ion.h>
#include <camera/CameraMetadata.h>

#include "buffer_manager.h"
#include "common_defs.h"
#include <modal_journal.h>

using namespace std;

static const char* ion_dev_file = "/dev/ion";

static int ionFd;

#define ALIGN_BYTE(x, a) ((x % a == 0) ? x : x - (x % a) + a)

// -----------------------------------------------------------------------------------------------------------------------------
// Moves the UV planes to be contiguous with the y plane
// -----------------------------------------------------------------------------------------------------------------------------
// void bufferMakeYUVContiguous(BufferBlock* pBufferInfo)
// {

//     const int height = pBufferInfo->height;
//     const int width  = pBufferInfo->width;
//     //(Total size - expected size) / 1.5 because there's padding at the end as well
//     const int offset = (pBufferInfo->size - (width * height * 1.5))/1.5;

//     memcpy((uint8_t*)(pBufferInfo->vaddress) + (width*height),
//            (uint8_t*)(pBufferInfo->vaddress) + (width*height) + offset,
//            width*height/2);
// }

int allocateOneBuffer(
        BufferGroup&       bufferGroup,
        unsigned int       index,
        unsigned int       width,
        unsigned int       height,
        unsigned int       format,
        unsigned long int  consumerFlags,
        buffer_handle_t*   pBuffer)
{
    BufferBlock &block = bufferGroup.bufferBlocks[index];
    struct ion_allocation_data allocation_data;
    native_handle_t* native_handle = nullptr;
    size_t buffer_size;
    unsigned int stride = 0;
    unsigned int slice = 0;

    if (ionFd <= 0) {
        ionFd = open(ion_dev_file, O_RDONLY);
        if (ionFd <= 0) {
            M_PRINT("Ion dev file open failed. Error=%d\n", errno);
            return -EINVAL;
        }
    }
    memset(&allocation_data, 0, sizeof(allocation_data));

    // this is for raw and color buffers, everything but jpeg
    if (format == HAL3_FMT_YUV ||
         (consumerFlags & GRALLOC_USAGE_HW_COMPOSER) ||
         (consumerFlags & GRALLOC_USAGE_HW_TEXTURE) ||
         (consumerFlags & GRALLOC_USAGE_SW_WRITE_OFTEN)) {


        /*
        Here is the logic for determining where CAMX is going to put the uv data
        after the Y data, there is usually a gap.
        TODO pull these alignment values from HAL3 instead of guessing
        */
        if(height<=480){
            M_DEBUG("ALIGNING HEIGHT FOR VGA\n");
            slice = ALIGN_BYTE(height, 64);
            //stride = ALIGN_BYTE(width, 64);
            stride = width;
        }
        else if(width==1280 && height==800){
            M_DEBUG("ALIGNING HEIGHT FOR OV9782\n");
            slice = ALIGN_BYTE(height, 64);
            stride = width;
        }
        else{
            M_DEBUG("ALIGNING HEIGHT FOR LARGE IMAGE\n");
            // WORKING WITH 214
            // slice = ALIGN_BYTE(height, 512);
            // stride = ALIGN_BYTE(width, 256);

            slice = ALIGN_BYTE(height, 512);
            stride = width;
        }

        // times 2 seems unnecessary
        // buffer_size = (size_t)(stride * slice * 2);

        // James changed this to 1.5 instead of 2 on May 24 2023, if it causes problems
        // change back to 2
        buffer_size = (size_t)(stride * slice * 1.5);

        M_DEBUG("Allocating img Buffer: width: %4d stride: %4d height: %4d slice: %4d size: %7d\n",
                    width,
                    stride,
                    height,
                    slice,
                    buffer_size);
    } else {
        // for "blob" allocation for jpeg only
        buffer_size = width;

        M_DEBUG("Allocating jpeg Buffer: size: %7d\n", buffer_size);
    }

    allocation_data.len = ((size_t)(buffer_size) + 4095U) & (~4095U);

    allocation_data.flags = 1;
    allocation_data.heap_id_mask = (1U << ION_SYSTEM_HEAP_ID);
    if (int ret = ioctl(ionFd, _IOWR('I', 0, struct ion_allocation_data), &allocation_data)) {
        M_PRINT("ION allocation failed. ret=%d Error=%d fd=%d\n", ret, errno, ionFd);
        return ret;
    }

    block.vaddress = mmap(NULL,
                        allocation_data.len,
                        PROT_READ  | PROT_WRITE,
                        MAP_SHARED,
                        allocation_data.fd,
                        0);

    if (block.vaddress < 0) {
        int errsv = errno;
        printf("mmap failed: %s (errno=%d)\n", strerror(errsv), errsv);
        return -EINVAL;
    }

    //M_DEBUG("allocated block at vaddr=0x%x len=0x%d\n", block.vaddress, allocation_data.len);

    block.size     = allocation_data.len;
    block.width    = width;
    block.height   = height;
    block.stride   = stride;
    block.slice    = slice;

    block.uvHead   = (block.vaddress) + (stride * slice);

    native_handle = native_handle_create(1, 4);
    (native_handle)->data[0] = allocation_data.fd;
    (native_handle)->data[1] = 0;
    (native_handle)->data[2] = 0;
    (native_handle)->data[3] = 0;
    (native_handle)->data[4] = allocation_data.len;
    (native_handle)->data[5] = 0;

    *pBuffer = native_handle;

    return 0;
}

void deleteOneBuffer(
       BufferGroup&       bufferGroup,
       unsigned int       index)
{
    if (bufferGroup.buffers[index] != NULL) {
        munmap(bufferGroup.bufferBlocks[index].vaddress, bufferGroup.bufferBlocks[index].size);
        native_handle_close((native_handle_t *)bufferGroup.buffers[index]);
        native_handle_delete((native_handle_t *)bufferGroup.buffers[index]);

        bufferGroup.buffers[index] = NULL;
    }
}

#endif
