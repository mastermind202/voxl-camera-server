/*******************************************************************************************************************************
 *
 * Copyright (c) 2019 ModalAI, Inc.
 *
 ******************************************************************************************************************************/
#include <stdlib.h>
#ifdef USE_GRALLOC1
#include <sys/mman.h>
#endif
#undef LOG_TAG
#define LOG_TAG "voxl-camera-server"
#include <errno.h>
#include "buffer_manager.h"
#include "common_defs.h"
#include "debug_log.h"

// -----------------------------------------------------------------------------------------------------------------------------
// Default constructor
// -----------------------------------------------------------------------------------------------------------------------------
BufferManager::BufferManager()
{
    m_numBuffers     = 0;
    m_ppBuffers      = NULL;
    m_pBufferInfo    = NULL;
    m_pGrallocDevice = NULL;
}

// -----------------------------------------------------------------------------------------------------------------------------
// Destructor
// -----------------------------------------------------------------------------------------------------------------------------
BufferManager::~BufferManager()
{
    Destroy();
}

// -----------------------------------------------------------------------------------------------------------------------------
// Once a buffer is returned from here, the buffer manager has no reference to it. The client has to explicitly return the
// buffer to the BufferManager once it is doing using it.
// -----------------------------------------------------------------------------------------------------------------------------
buffer_handle_t* BufferManager::GetBuffer()
{
    std::unique_lock<std::mutex> lock(m_mutex);

    if (m_availableBuffers.size() == 0)
    {
        m_condVar.wait(lock);
    }

    buffer_handle_t* buffer = m_availableBuffers.front();
    m_availableBuffers.pop_front();

    return buffer;
}

// -----------------------------------------------------------------------------------------------------------------------------
// Once the client is done using the buffer it can return it to the BufferManager with this interface call. Once the buffer is
// returned to the BufferManager it will be made available for re-use
// -----------------------------------------------------------------------------------------------------------------------------
void BufferManager::PutBuffer(buffer_handle_t* buffer)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    m_availableBuffers.push_back(buffer);
    m_condVar.notify_all();
}

// -----------------------------------------------------------------------------------------------------------------------------
// Returns the buffer information structure for the requested buffer handle
// -----------------------------------------------------------------------------------------------------------------------------
BufferInfo* BufferManager::GetBufferInfo(buffer_handle_t* buffer)
{
    BufferInfo* pBufferInfo = NULL;
    std::unique_lock<std::mutex> lock(m_mutex);

    for (uint32_t i = 0; i < m_numBuffers; i++)
    {
        if (*buffer == m_ppBuffers[i])
        {
            pBufferInfo = &(m_pBufferInfo[i]);
        }
    }

    return pBufferInfo;
}

// -----------------------------------------------------------------------------------------------------------------------------
// Called before destroying the object. Free's all allocated resources.
// -----------------------------------------------------------------------------------------------------------------------------
void BufferManager::Destroy()
{
    FreeAllBuffers();

    if (m_pGrallocDevice != NULL)
    {
#ifdef USE_GRALLOC1
        gralloc1_close(m_pGrallocDevice);
#else
        gralloc_close(m_pGrallocDevice);
#endif
    }
}

// -----------------------------------------------------------------------------------------------------------------------------
// Returns a buffer that contains contiguous YUV data
// -----------------------------------------------------------------------------------------------------------------------------
uint8_t* BufferManager::MakeYUVContiguous(BufferInfo* pBufferInfo)
{
    if (m_pYUVContiguousData != NULL)
    {
        if (pBufferInfo->stride == pBufferInfo->width)
        {
            memcpy(m_pYUVContiguousData, pBufferInfo->vaddr, m_yChannelBytes);

            if (pBufferInfo->format == HAL_PIXEL_FORMAT_YCbCr_420_888)
            {
                if ((unsigned long)pBufferInfo->craddr < (unsigned long)pBufferInfo->cbaddr)
                {
                    memcpy(m_pUVContiguousData, pBufferInfo->craddr, m_uvChannelBytes);
                }
                else
                {
                    memcpy(m_pUVContiguousData, pBufferInfo->cbaddr, m_uvChannelBytes);
                }
            }
            else
            {
                ///<@todo Need to handle the formats ending up here
                memcpy(m_pUVContiguousData, pBufferInfo->craddr, m_uvChannelBytes);
            }
        }
        else
        {
            int height      = pBufferInfo->height;
            int width       = pBufferInfo->width;
            int stride      = pBufferInfo->stride;
            uint8_t* yAddr  = (uint8_t*)(pBufferInfo->vaddr);
            uint8_t* uvAddr = NULL;

            if ((unsigned long)pBufferInfo->craddr < (unsigned long)pBufferInfo->cbaddr)
            {
                uvAddr = (uint8_t*)(pBufferInfo->craddr);
            }
            else
            {
                uvAddr = (uint8_t*)(pBufferInfo->cbaddr);
            }

            for (int i = 0; i < height; i++)
            {
                memcpy(m_pYUVContiguousData+(i*width), yAddr+(i*stride), width);
            }
            for (int i = 0; i < height/2; i++)
            {
                memcpy(m_pUVContiguousData+(i*width), uvAddr+(i*stride), width);
            }
        }
    }

    return m_pYUVContiguousData;
}

// -----------------------------------------------------------------------------------------------------------------------------
// Do a one time initialization including setting up the Gralloc interface
// -----------------------------------------------------------------------------------------------------------------------------
int BufferManager::Initialize(uint32_t numBuffers)
{
    int status = 0;

    m_pYUVContiguousData = NULL;

    status = SetupGrallocInterface();

    if (status == 0)
    {
        m_numBuffers  = numBuffers;
        m_ppBuffers   = new buffer_handle_t[m_numBuffers]; // buffer_handle_t is native_handle_t*
        m_pBufferInfo = new BufferInfo[m_numBuffers];

        if ((m_ppBuffers == NULL) || (m_pBufferInfo == NULL))
        {
            m_numBuffers = 0;
            status = -ENOMEM;
        }
        else
        {
            for (uint32_t i = 0; i < m_numBuffers; i++)
            {
                m_pBufferInfo[i].vaddr  = NULL;
                m_pBufferInfo[i].cbaddr = NULL;
                m_pBufferInfo[i].craddr = NULL;

                m_ppBuffers[i] = NULL;
            }
        }
    }

    return status;
}

// -----------------------------------------------------------------------------------------------------------------------------
// Sets up the gralloc interface to be used for making the buffer memory allocation and lock/unlock/free calls
// -----------------------------------------------------------------------------------------------------------------------------
int BufferManager::SetupGrallocInterface()
{
    int status = 0;

    hw_get_module(GRALLOC_HARDWARE_MODULE_ID, const_cast<const hw_module_t**>(&m_hwModule));

#ifdef USE_GRALLOC1
    if (NULL != m_hwModule)
    {
        gralloc1_open(m_hwModule, &m_pGrallocDevice);
    }
    else
    {
        VOXL_LOG_FATAL("voxl-camera-server ERROR: Can not get Gralloc1 hardware module\n\n");
        status = -1;
    }

    if (NULL != m_pGrallocDevice)
    {
        m_pGrallocInterface.CreateDescriptor  = reinterpret_cast<GRALLOC1_PFN_CREATE_DESCRIPTOR>(
            m_pGrallocDevice->getFunction(m_pGrallocDevice, GRALLOC1_FUNCTION_CREATE_DESCRIPTOR));

        m_pGrallocInterface.DestroyDescriptor = reinterpret_cast<GRALLOC1_PFN_DESTROY_DESCRIPTOR>(
            m_pGrallocDevice->getFunction(m_pGrallocDevice, GRALLOC1_FUNCTION_DESTROY_DESCRIPTOR));

        m_pGrallocInterface.SetDimensions     = reinterpret_cast<GRALLOC1_PFN_SET_DIMENSIONS>(
            m_pGrallocDevice->getFunction(m_pGrallocDevice, GRALLOC1_FUNCTION_SET_DIMENSIONS));

        m_pGrallocInterface.SetFormat         = reinterpret_cast<GRALLOC1_PFN_SET_FORMAT>(
            m_pGrallocDevice->getFunction(m_pGrallocDevice, GRALLOC1_FUNCTION_SET_FORMAT));

        m_pGrallocInterface.SetProducerUsage  = reinterpret_cast<GRALLOC1_PFN_SET_PRODUCER_USAGE>(
            m_pGrallocDevice->getFunction(m_pGrallocDevice, GRALLOC1_FUNCTION_SET_PRODUCER_USAGE));

        m_pGrallocInterface.SetConsumerUsage  = reinterpret_cast<GRALLOC1_PFN_SET_CONSUMER_USAGE>(
            m_pGrallocDevice->getFunction(m_pGrallocDevice, GRALLOC1_FUNCTION_SET_CONSUMER_USAGE));

        m_pGrallocInterface.Allocate          = reinterpret_cast<GRALLOC1_PFN_ALLOCATE>(
            m_pGrallocDevice->getFunction(m_pGrallocDevice, GRALLOC1_FUNCTION_ALLOCATE));

        m_pGrallocInterface.GetStride         = reinterpret_cast<GRALLOC1_PFN_GET_STRIDE>(
            m_pGrallocDevice->getFunction(m_pGrallocDevice, GRALLOC1_FUNCTION_GET_STRIDE));

        m_pGrallocInterface.Release           = reinterpret_cast<GRALLOC1_PFN_RELEASE>(
            m_pGrallocDevice->getFunction(m_pGrallocDevice, GRALLOC1_FUNCTION_RELEASE));

        m_pGrallocInterface.Lock              = reinterpret_cast<GRALLOC1_PFN_LOCK>(
            m_pGrallocDevice->getFunction(m_pGrallocDevice, GRALLOC1_FUNCTION_LOCK));
    }
    else
    {
        VOXL_LOG_FATAL("voxl-camera-server ERROR: Gralloc1_open failed\n\n");
        status = -1;
    }
#else
    if (NULL != m_hwModule)
    {
        gralloc_open(m_hwModule, &m_pGrallocDevice);

        if (NULL == m_pGrallocDevice)
        {
            VOXL_LOG_FATAL("voxl-camera-server ERROR: Can not get Gralloc device!\n\n");
            status = -EINVAL;
        }
    }
    else
    {
        VOXL_LOG_FATAL("voxl-camera-server ERROR: Can not get Gralloc hardware module\n\n");
        status = -EINVAL;
    }
#endif

    return status;
}

// -----------------------------------------------------------------------------------------------------------------------------
// Allocate buffer memory for all buffers based on input paramaters
// -----------------------------------------------------------------------------------------------------------------------------
int BufferManager::AllocateBuffers(uint32_t width,          ///< Buffer width
                                   uint32_t height,         ///< Buffer height
                                   uint32_t format,         ///< Buffer format
                                   uint64_t producerFlags,  ///< Gralloc flags indicator for the camera module
                                   uint64_t consumerFlags)  ///< Gralloc flags indicator about how we will use the buffers
{
    int status = 0;

    // For the TOF camera we have to send the BLOB format buffers to the camera module but these are not jpg images. So we  
    // cant really compute the size for a jpeg image hence just make it twice the size of the (width * height)  
    if (format == HAL_PIXEL_FORMAT_BLOB)    
    {   
        width  = width * height * 2;    
        height = 1; 
    }

    for (uint32_t i = 0; i < m_numBuffers; i++)
    {
        status = AllocateOneBuffer(width, height, format, producerFlags, consumerFlags, &m_ppBuffers[i], &m_bufferStride, i);

        if (status != 0)
        {
            VOXL_LOG_FATAL("voxl-camera-server ERROR: Buffer allocation failure!\n");
            Destroy();
            break;
        }

        m_availableBuffers.push_back(&m_ppBuffers[i]);
    }

    if (status == 0)
    {
        // Look at the first buffer to determine if there are any gaps between Y and UV data. All other buffes will be
        // the same
        unsigned long yAddr              = (unsigned long)m_pBufferInfo[0].vaddr;
        unsigned int firstChromaAddress = 0;

        if (format == HAL_PIXEL_FORMAT_YCbCr_420_888)
        {
            if ((unsigned long)m_pBufferInfo[0].craddr < (unsigned long)m_pBufferInfo[0].cbaddr)
            {
                firstChromaAddress = (unsigned long)m_pBufferInfo[0].craddr;
            }
            else
            {
                firstChromaAddress = (unsigned long)m_pBufferInfo[0].cbaddr;
            }

            m_yChannelBytes  = (m_bufferStride * height);
            m_uvChannelBytes = (m_yChannelBytes >> 1); // YCbCr_420 is 12 bpp

            ///<@todo Need to handle NV12
            if ((yAddr + (unsigned int)(height * m_pBufferInfo[0].stride)) != firstChromaAddress)
            {
                m_isYUVContiguous = 0;

                int allocateBytes = (m_yChannelBytes + m_uvChannelBytes);

                m_pYUVContiguousData = new uint8_t [allocateBytes];

                if (m_pYUVContiguousData == NULL)
                {
                    status = S_OUTOFMEM;
                }
                else
                {
                    m_pUVContiguousData = m_pYUVContiguousData + (m_bufferStride * height);
                }
            }
            else
            {
                m_isYUVContiguous = 1;
            }
        }
        else
        {
            m_isYUVContiguous = 1;
        }
    }

    return status;
}

// -----------------------------------------------------------------------------------------------------------------------------
// Call the Gralloc interface to do the actual memory allocation for one single buffer
// -----------------------------------------------------------------------------------------------------------------------------
int BufferManager::AllocateOneBuffer(uint32_t         width,                ///< Buffer width
                                     uint32_t         height,               ///< Buffer height
                                     uint32_t         format,               ///< Buffer format
                                     uint64_t         producerUsageFlags,   ///< Gralloc flags indicator for the camera module
                                     uint64_t         consumerUsageFlags,   ///< Gralloc flags indicating our buffer usage
                                     buffer_handle_t* pAllocatedBuffer,     ///< Returned buffer that has been allocated
                                     uint32_t*        pStride,              ///< Stride of the allocated buffer
                                     uint32_t         index)                ///< Buffer number indicator
{
    int      status    =  0;
    uint32_t imageSize = 0;

    m_pBufferInfo[index].vaddr  = NULL;
    m_pBufferInfo[index].cbaddr = NULL;
    m_pBufferInfo[index].craddr = NULL;
    m_pBufferInfo[index].format = format;
    m_pBufferInfo[index].width  = width;
    m_pBufferInfo[index].height = height;

#ifdef USE_GRALLOC1
    status = GRALLOC1_ERROR_NONE;
    gralloc1_buffer_descriptor_t gralloc1BufferDescriptor;

    status = m_pGrallocInterface.CreateDescriptor(m_pGrallocDevice, &gralloc1BufferDescriptor);

    if (GRALLOC1_ERROR_NONE == status)
    {
        status = m_pGrallocInterface.SetDimensions(m_pGrallocDevice, gralloc1BufferDescriptor, width, height);
    }

    if (GRALLOC1_ERROR_NONE == status)
    {
        status = m_pGrallocInterface.SetFormat(m_pGrallocDevice, gralloc1BufferDescriptor, format);
    }

    if (GRALLOC1_ERROR_NONE == status)
    {
        status = m_pGrallocInterface.SetProducerUsage(m_pGrallocDevice, gralloc1BufferDescriptor, producerUsageFlags);
    }

    if (GRALLOC1_ERROR_NONE == status)
    {
        status = m_pGrallocInterface.SetConsumerUsage(m_pGrallocDevice, gralloc1BufferDescriptor, consumerUsageFlags);
    }

    if (GRALLOC1_ERROR_NONE == status)
    {
        status = m_pGrallocInterface.Allocate(m_pGrallocDevice, 1, &gralloc1BufferDescriptor, &pAllocatedBuffer[0]);
    }

    if (GRALLOC1_ERROR_NONE == status)
    {
        status = m_pGrallocInterface.GetStride(m_pGrallocDevice, *pAllocatedBuffer, pStride);
    }

    if (GRALLOC1_ERROR_NONE == status)
    {
        m_pBufferInfo[index].stride = *pStride;

        imageSize = *pStride * height;

        if (format == HAL_PIXEL_FORMAT_YCbCr_420_888)
        {
            imageSize = *pStride * height * 1.5; // 1.5 because it is 12 bits / pixel
        }

        m_pBufferInfo[index].vaddr = mmap(NULL, imageSize, PROT_READ  | PROT_WRITE, MAP_SHARED, (*pAllocatedBuffer)->data[0], 0);

        if (format == HAL_PIXEL_FORMAT_YCbCr_420_888)
        {
            uint8_t* pYAddress = (uint8_t*)m_pBufferInfo[index].vaddr;

            m_pBufferInfo[index].craddr = (pYAddress + (*pStride * height));
            m_pBufferInfo[index].craddr = ((pYAddress + (*pStride * height)) + 1);
        }
    }

    if (GRALLOC1_ERROR_NONE != status)
    {
        VOXL_LOG_FATAL("voxl-camera-server ERROR: Allocate buffer failed\n\n");
    }

    m_pGrallocInterface.DestroyDescriptor(m_pGrallocDevice, gralloc1BufferDescriptor);
#else
    // Call gralloc to make the memory allocation
    m_pGrallocDevice->alloc(m_pGrallocDevice,
                            width,
                            height,
                            format,
                            (int)(producerUsageFlags | consumerUsageFlags),
                            pAllocatedBuffer,
                            (int*)pStride);

    m_pBufferInfo[index].stride = *pStride;

    imageSize = *pStride * height;

    gralloc_module_t* pGrallocModule = (gralloc_module_t*)m_hwModule;

    // Get the CPU virtual address of the buffer memory allocation
    if (format == HAL_PIXEL_FORMAT_RAW10)
    {
        pGrallocModule->lock(pGrallocModule,
                             *pAllocatedBuffer,
                             0,
                             0,
                             0,
                             width,
                             height,
                             &m_pBufferInfo[index].vaddr);
    }
    else if (format == HAL_PIXEL_FORMAT_YCbCr_420_888)
    {
        struct android_ycbcr ycbcr;
        status = pGrallocModule->lock_ycbcr(pGrallocModule,
                                            *pAllocatedBuffer,
                                            GRALLOC_USAGE_SW_READ_OFTEN,
                                            0,
                                            0,
                                            width,
                                            height,
                                            &ycbcr);

        m_pBufferInfo[index].vaddr  = ycbcr.y;
        m_pBufferInfo[index].cbaddr = ycbcr.cb;
        m_pBufferInfo[index].craddr = ycbcr.cr;

        imageSize = *pStride * height * 1.5; // 1.5 because it is 12 bits / pixel
    }
    else if (format == HAL_PIXEL_FORMAT_BLOB)
    {
        imageSize = width;

        pGrallocModule->lock(pGrallocModule,
                             *pAllocatedBuffer,
                             GRALLOC_USAGE_SW_READ_OFTEN,
                             0,
                             0,
                             width,
                             height,
                             &m_pBufferInfo[index].vaddr);
    }
    else
    {
        VOXL_LOG_FATAL("voxl-camera-server ERROR: Unknown pixel format!\n");
    }
#endif

    m_pBufferInfo[index].size = imageSize;

    if (m_pBufferInfo[index].vaddr == NULL)
    {
        VOXL_LOG_FATAL("voxl-camera-server ERROR: Cannot map buffer allocation!\n");
        status = -EINVAL;
    }

    return status;
}

// -----------------------------------------------------------------------------------------------------------------------------
// Free the memory allocation for all the buffers
// -----------------------------------------------------------------------------------------------------------------------------
void BufferManager::FreeAllBuffers()
{
#ifndef USE_GRALLOC1
    gralloc_module_t* pGrallocModule = (gralloc_module_t*)m_hwModule;
#endif

    for (uint32_t i = 0; i < m_numBuffers; i++)
    {
        if (NULL != m_ppBuffers[i])
        {
#ifdef USE_GRALLOC1
            munmap(m_pBufferInfo[i].vaddr, m_pBufferInfo[i].size);
            m_pGrallocInterface.Release(m_pGrallocDevice, m_ppBuffers[i]);
#else
            pGrallocModule->unlock(pGrallocModule, m_ppBuffers[i]);
            m_pGrallocDevice->free(m_pGrallocDevice, m_ppBuffers[i]);
#endif
            m_ppBuffers[i] = NULL;
        }
    }

    if (m_ppBuffers != NULL)
    {
        delete m_ppBuffers;
    }

    if (m_pBufferInfo != NULL)
    {
        delete m_pBufferInfo;
    }

    if (m_pYUVContiguousData != NULL)
    {
        delete m_pYUVContiguousData;
    }

    m_availableBuffers.clear();
}
