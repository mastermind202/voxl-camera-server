/*******************************************************************************
 * Copyright 2020 ModalAI Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * 4. The Software is used solely in conjunction with devices provided by
 *    ModalAI Inc.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 ******************************************************************************/
#ifndef VOXL_CAMERA_SERVER_VIDEO_ENCODER
#define VOXL_CAMERA_SERVER_VIDEO_ENCODER

#include <list>
#include <OMX_Core.h>
#include <OMX_IVCommon.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <buffer_manager.h>

typedef enum encode_target_t{
    TARGET_STREAM,
    TARGET_RECORD
} encode_target_t;

// -----------------------------------------------------------------------------------------------------------------------------
// Video encoder config data
// -----------------------------------------------------------------------------------------------------------------------------
typedef struct VideoEncoderConfig
{
    uint32_t width;                 ///< Image width
    uint32_t height;                ///< Image height
    uint32_t format;                ///< Image format
    bool     isBitRateConstant;     ///< Is the bit rate constant
    int      targetBitRate;         ///< Desired target bitrate
    int32_t  frameRate;             ///< Frame rate
    bool     isH265;                ///< Is it H265 encoding or H264
    BufferGroup* inputBuffers;      ///< Input buffers coming from hal3
    int*     outputPipe;            ///< Pre-configured MPA output pipe
    encode_target_t target;         ///< TARGET_STREAM or TARGET_RECORD
} VideoEncoderConfig;

//------------------------------------------------------------------------------------------------------------------------------
// Main interface class that interacts with the OMX Encoder component and the Camera Manager class. At the crux of it, this
// class takes the YUV frames from the camera and passes it to the OMX component for encoding. It gets the final encoded frames
// in h264 or h265 format
//------------------------------------------------------------------------------------------------------------------------------
class VideoEncoder
{
public:
    VideoEncoder(VideoEncoderConfig* pVideoEncoderConfig);
    ~VideoEncoder();

    // Do any necessary work to start receiving encoding frames from the client
    void Start();
    // This call indicates that no more frames will be sent for encoding
    void Stop();
    // Client of this encoder class calls this function to pass in the YUV video frame to be encoded
    void ProcessFrameToEncode(camera_image_metadata_t meta, BufferBlock* buffer);

    void* ThreadProcessOMXOutputPort();

    int ItemsInQueue(); // return how many frames are still in the process queue

    // Set the OMX component configuration
    OMX_ERRORTYPE SetConfig(VideoEncoderConfig* pVideoEncoderConfig);
    // Set input / output port parameters
    OMX_ERRORTYPE SetPortParams(OMX_U32  portIndex,
                                OMX_U32  width,
                                OMX_U32  height,
                                OMX_U32  bufferCountMin,
                                OMX_U32  frameRate,
                                OMX_U32  bitrate,
                                OMX_U32* pBufferSize,
                                OMX_U32* pBufferCount,
                                OMX_COLOR_FORMATTYPE format);

    static const OMX_U32  PortIndexIn          = 0;
    static const OMX_U32  PortIndexOut         = 1;

    pthread_t              out_thread;              ///< Out thread
    pthread_mutex_t        out_mutex;               ///< Out thread Mutex for list access
    pthread_cond_t         out_cond;                ///< Out thread Condition variable for wake up
    std::list<OMX_BUFFERHEADERTYPE*>      out_msgQueue;            ///< Out thread Message queue
    std::list<camera_image_metadata_t>    out_metaQueue;           ///< Out thread Message queue

    volatile bool          stop = false;            ///< Thread terminate indicator

    VideoEncoderConfig     m_VideoEncoderConfig;
    int*                   m_outputPipe;
    uint32_t               m_inputBufferSize;       ///< Input buffer size
    uint32_t               m_inputBufferCount;      ///< Input buffer count
    uint32_t               m_outputBufferSize;      ///< Output buffer size
    uint32_t               m_outputBufferCount;     ///< Output buffer count
    OMX_HANDLETYPE         m_OMXHandle = NULL;      ///< OMX component handle
    BufferGroup*           m_pHALInputBuffers;
    OMX_BUFFERHEADERTYPE** m_ppInputBuffers;        ///< Input buffers
    uint32_t               m_nextInputBufferIndex;  ///< Next input buffer to use
    OMX_BUFFERHEADERTYPE** m_ppOutputBuffers;       ///< Output buffers
    uint32_t               m_nextOutputBufferIndex; ///< Next input buffer to use
};



static void _print_omx_error(OMX_ERRORTYPE e)
{
    switch(e) {
        case OMX_ErrorNone:
            M_ERROR("OMX_ErrorNone\n");
            break;
        case OMX_ErrorInsufficientResources:
            M_ERROR("OMX_ErrorInsufficientResources\n");
            break;
        case OMX_ErrorUndefined:
            M_ERROR("OMX_ErrorUndefined\n");
            break;
        case OMX_ErrorInvalidComponentName:
            M_ERROR("OMX_ErrorInvalidComponentName\n");
            break;
        case OMX_ErrorComponentNotFound:
            M_ERROR("OMX_ErrorComponentNotFound\n");
            break;
        case OMX_ErrorInvalidComponent:
            M_ERROR("OMX_ErrorInvalidComponent\n");
            break;
        case OMX_ErrorBadParameter:
            M_ERROR("OMX_ErrorBadParameter\n");
            break;
        case OMX_ErrorNotImplemented:
            M_ERROR("OMX_ErrorNotImplemented\n");
            break;
        case OMX_ErrorUnderflow:
            M_ERROR("OMX_ErrorUnderflow\n");
            break;
        case OMX_ErrorOverflow:
            M_ERROR("OMX_ErrorOverflow\n");
            break;
        case OMX_ErrorHardware:
            M_ERROR("OMX_ErrorHardware\n");
            break;
        case OMX_ErrorInvalidState:
            M_ERROR("OMX_ErrorInvalidState\n");
            break;
        case OMX_ErrorStreamCorrupt:
            M_ERROR("OMX_ErrorStreamCorrupt\n");
            break;
        case OMX_ErrorPortsNotCompatible:
            M_ERROR("OMX_ErrorPortsNotCompatible\n");
            break;
        case OMX_ErrorResourcesLost:
            M_ERROR("OMX_ErrorResourcesLost\n");
            break;
        case OMX_ErrorNoMore:
            M_ERROR("OMX_ErrorNoMore\n");
            break;
        case OMX_ErrorVersionMismatch:
            M_ERROR("OMX_ErrorVersionMismatch\n");
            break;
        case OMX_ErrorNotReady:
            M_ERROR("OMX_ErrorNotReady\n");
            break;
        case OMX_ErrorTimeout:
            M_ERROR("OMX_ErrorTimeout\n");
            break;
        case OMX_ErrorSameState:
            M_ERROR("OMX_ErrorSameState\n");
            break;
        case OMX_ErrorResourcesPreempted:
            M_ERROR("OMX_ErrorResourcesPreempted\n");
            break;
        case OMX_ErrorPortUnresponsiveDuringAllocation:
            M_ERROR("OMX_ErrorPortUnresponsiveDuringAllocation\n");
            break;
        case OMX_ErrorPortUnresponsiveDuringDeallocation:
            M_ERROR("OMX_ErrorPortUnresponsiveDuringDeallocation\n");
            break;
        case OMX_ErrorPortUnresponsiveDuringStop:
            M_ERROR("OMX_ErrorPortUnresponsiveDuringStop\n");
            break;
        case OMX_ErrorIncorrectStateTransition:
            M_ERROR("OMX_ErrorIncorrectStateTransition\n");
            break;
        case OMX_ErrorIncorrectStateOperation:
            M_ERROR("OMX_ErrorIncorrectStateOperation\n");
            break;
        case OMX_ErrorUnsupportedSetting:
            M_ERROR("OMX_ErrorUnsupportedSetting\n");
            break;
        case OMX_ErrorUnsupportedIndex:
            M_ERROR("OMX_ErrorUnsupportedIndex\n");
            break;
        case OMX_ErrorBadPortIndex:
            M_ERROR("OMX_ErrorBadPortIndex\n");
            break;
        case OMX_ErrorPortUnpopulated:
            M_ERROR("OMX_ErrorPortUnpopulated\n");
            break;
        case OMX_ErrorComponentSuspended:
            M_ERROR("OMX_ErrorComponentSuspended\n");
            break;
        case OMX_ErrorDynamicResourcesUnavailable:
            M_ERROR("OMX_ErrorDynamicResourcesUnavailable\n");
            break;
        case OMX_ErrorMbErrorsInFrame:
            M_ERROR("OMX_ErrorMbErrorsInFrame\n");
            break;
        case OMX_ErrorFormatNotDetected:
            M_ERROR("OMX_ErrorFormatNotDetected\n");
            break;
        case OMX_ErrorContentPipeOpenFailed:
            M_ERROR("OMX_ErrorContentPipeOpenFailed\n");
            break;
        case OMX_ErrorContentPipeCreationFailed:
            M_ERROR("OMX_ErrorContentPipeCreationFailed\n");
            break;
        case OMX_ErrorSeperateTablesUsed:
            M_ERROR("OMX_ErrorSeperateTablesUsed\n");
            break;
        case OMX_ErrorTunnelingUnsupported:
            M_ERROR("OMX_ErrorTunnelingUnsupported\n");
            break;
        case OMX_ErrorKhronosExtensions:
            M_ERROR("OMX_ErrorKhronosExtensions\n");
            break;
        case OMX_ErrorVendorStartUnused:
            M_ERROR("OMX_ErrorVendorStartUnused\n");
            break;
        case OMX_ErrorMax:
            M_ERROR("OMX_ErrorMax\n");
            break;
    }
    return;
}

#endif // VOXL_CAMERA_SERVER_VIDEO_ENCODER
