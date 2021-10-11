/*******************************************************************************************************************************
 *
 * Copyright (c) 2019 ModalAI, Inc.
 *
 ******************************************************************************************************************************/
#ifndef BUFFER_MANAGER
#define BUFFER_MANAGER

#include <condition_variable>
#include <deque>
#ifdef USE_GRALLOC1
#include <hardware/gralloc1.h>
#else
#include <hardware/gralloc.h>
#endif
#include <mutex>

// -----------------------------------------------------------------------------------------------------------------------------
// Structure containing the buffer information
// -----------------------------------------------------------------------------------------------------------------------------
typedef struct BufferInfo
{
    void*    vaddr;         ///< CPU virtual address (vaddr) to the start of the buffer allocation
    void*    cbaddr;        ///< CPU address to the Cb channel
    void*    craddr;        ///< CPU address to the Cr channel
    int      size;          ///< Size of the allocation
    int      fd;            ///< File descriptor of the allocation
    uint32_t format;        ///< Buffer format
    uint32_t width;         ///< Buffer width
    uint32_t height;        ///< Buffer height
    uint32_t stride;        ///< Buffer stride
} BufferInfo;

#ifdef USE_GRALLOC1
/// @brief Gralloc1 interface functions
struct Gralloc1Interface
{
    int32_t (*CreateDescriptor)(
        gralloc1_device_t*             pGralloc1Device,
        gralloc1_buffer_descriptor_t*  pCreatedDescriptor);
    int32_t (*DestroyDescriptor)(
        gralloc1_device_t*            pGralloc1Device,
        gralloc1_buffer_descriptor_t  descriptor);
    int32_t (*SetDimensions)(
        gralloc1_device_t*           pGralloc1Device,
        gralloc1_buffer_descriptor_t descriptor,
        uint32_t                       width,
        uint32_t                       height);
    int32_t (*SetFormat)(
        gralloc1_device_t*           pGralloc1Device,
        gralloc1_buffer_descriptor_t descriptor,
        int32_t                        format);
    int32_t (*SetProducerUsage)(
        gralloc1_device_t*           pGralloc1Device,
        gralloc1_buffer_descriptor_t descriptor,
        uint64_t                       usage);
    int32_t (*SetConsumerUsage)(
        gralloc1_device_t*           pGralloc1Device,
        gralloc1_buffer_descriptor_t descriptor,
        uint64_t                       usage);
    int32_t (*Allocate)(
        gralloc1_device_t*                  pGralloc1Device,
        uint32_t                              numDescriptors,
        const gralloc1_buffer_descriptor_t* pDescriptors,
        buffer_handle_t*                    pAllocatedBuffers);
    int32_t (*GetStride)(
        gralloc1_device_t* pGralloc1Device,
        buffer_handle_t    buffer,
        uint32_t*            pStride);
    int32_t (*Release)(
        gralloc1_device_t* pGralloc1Device,
        buffer_handle_t    buffer);
    int32_t (*Lock)(
        gralloc1_device_t*      device,
        buffer_handle_t         buffer,
        uint64_t                producerUsage,
        uint64_t                consumerUsage,
        const gralloc1_rect_t*  accessRegion,
        void**                  outData,
        int32_t                 acquireFence);
};
#endif

// -----------------------------------------------------------------------------------------------------------------------------
// Class that manages the buffer allocation and provides buffers to the client as and when requested. Physical memory
// allocation could be done using Gralloc, Gralloc1, Ion or any other mechanism. Those details can be hidden in this class
// because the client doesn't need to know about it.
// -----------------------------------------------------------------------------------------------------------------------------
class BufferManager
{
public:
    BufferManager();
    ~BufferManager();

    // The number of buffers that are currently available with the BufferManager. This does not include the buffers already
    // grabed by the client before using GetBuffer()
    size_t QueueSize()
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_availableBuffers.size();
    }
    // Is the YUV data contiguous for the buffers managed by this buffer manager
    bool IsYUVDataContiguous()
    {
        return m_isYUVContiguous;
    }
    // Get stride
    uint32_t GetStride() const
    {
        return m_bufferStride;
    }
    // Get pointer to contiguous YUV data
    uint8_t* MakeYUVContiguous(BufferInfo* pBufferInfo);
    // Get a buffer from the buffer manager that can be submitted to the camera module
    buffer_handle_t* GetBuffer();
    // Get the buffer information
    BufferInfo* GetBufferInfo(buffer_handle_t* buffer);
    // Release the buffer back to the BufferManager. BufferManager will put up the buffer for reuse immediately after this call
    void PutBuffer(buffer_handle_t* buffer);    
    // Perform any one time initialization
    int  Initialize(uint32_t numBuffers);
    // Destroy the object and release any resources that were created
    void Destroy();
    // Allocate memory for all the buffers
    int  AllocateBuffers(uint32_t width,
                         uint32_t height,
                         uint32_t format,
                         uint64_t producerFlags,
                         uint64_t consumerFlags);

private:
    // Disallow copy constructor or assignment operator
    BufferManager(const BufferManager&) = delete;
    BufferManager& operator= (const BufferManager&) = delete;

    // Setup the gralloc interface
    int SetupGrallocInterface();
    // Allocate memory for one buffer
    int AllocateOneBuffer(uint32_t         width,
                          uint32_t         height,
                          uint32_t         format,
                          uint64_t         producerUsageFlags,
                          uint64_t         consumerUsageFlags,
                          buffer_handle_t* pAllocatedBuffer,
                          uint32_t*        pStride,
                          uint32_t         index);
    // Free all the buffer memory
    void FreeAllBuffers();

    int                          m_isYUVContiguous;     ///< Is Y channel data immediately followed by UV channel data
                                                        ///  with no gaps in between
    uint8_t*                     m_pYUVContiguousData;  ///< Pointer to memory that contains contiguous YUV data
    uint8_t*                     m_pUVContiguousData;   ///< Pointer to UV memory section in m_pYUVContiguousData
    uint32_t                     m_yChannelBytes;       ///< Size in bytes of the Y channel
    uint32_t                     m_uvChannelBytes;      ///< Size in bytes of the UV channel
    buffer_handle_t*             m_ppBuffers;           ///< List of buffers managed by this class
    BufferInfo*                  m_pBufferInfo;         ///< Information about each buffer
    uint32_t                     m_numBuffers;          ///< Total num of buffers managed
    uint32_t                     m_bufferStride;        ///< Buffer stride
    std::deque<buffer_handle_t*> m_availableBuffers;    ///< All the available buffers at any given time
    std::mutex                   m_mutex;               ///< Mutex for accessing the buffer list
    std::condition_variable      m_condVar;             ///< Condition variable for accessing the buffer list
    hw_module_t*                 m_hwModule;            ///< Gralloc module
#ifdef USE_GRALLOC1
    gralloc1_device_t*           m_pGrallocDevice;      ///< Gralloc1 device
    Gralloc1Interface            m_pGrallocInterface;   ///< Gralloc1 interface
#else
    alloc_device_t*              m_pGrallocDevice;      ///< Gralloc device
#endif
};

#endif // BUFFER_MANAGER
