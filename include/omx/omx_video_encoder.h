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

    static const uint32_t BitrateDefault       = (2*8*1024*1024);
    static const uint32_t TargetBitrateDefault = (18*1024*1024*8);
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

#endif // VOXL_CAMERA_SERVER_VIDEO_ENCODER
