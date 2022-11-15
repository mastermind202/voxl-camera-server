/*******************************************************************************
 * Copyright 2022 ModalAI Inc.
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
#include <OMX_Component.h>
#include <OMX_IndexExt.h>
#include <cstring>
#include <dlfcn.h>
#include <unistd.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <errno.h>
#include <system/graphics.h>
#include <media/hardware/HardwareAPI.h>
#include <OMX_QCOMExtns.h>
#include <modal_journal.h>
#include <OMX_VideoExt.h>
#include <voxl_cutils.h>
#include <modal_pipe.h>

#include "omx_video_encoder.h"
#include "buffer_manager.h"
#include "common_defs.h"

#define USE_HAL_INPUT_BUFFERS

#define NUM_INPUT_BUFFERS  16
#define NUM_OUTPUT_BUFFERS 16

///<@todo Make these functions
#define Log2(number, power) {OMX_U32 temp = number; power = 0; while ((0 == (temp & 0x1)) && power < 16) {temp >>=0x1; power++;}}
#define FractionToQ16(q,num,den) { OMX_U32 power; Log2(den,power); q = num << (16 - power); }

///<@todo Need to come up with a better way
void* g_pOmxCoreHandle = NULL;

static const int32_t OMXSpecVersion = 0x00000101;

// Helper MACRO to reset the size, version of any OMX structure
#define OMX_RESET_STRUCT_SIZE_VERSION(_structPointer_, _structName_)    \
    (_structPointer_)->nSize = sizeof(_structName_);                    \
    (_structPointer_)->nVersion.nVersion = OMXSpecVersion

// Helper MACRO to reset any OMX structure to its default valid state
#define OMX_RESET_STRUCT(_structPointer_, _structName_)     \
    memset((_structPointer_), 0x0, sizeof(_structName_));   \
    (_structPointer_)->nSize = sizeof(_structName_);        \
    (_structPointer_)->nVersion.nVersion = OMXSpecVersion

// Main thread functions for providing input buffer to the OMX input port and processing encoded buffer on the OMX output port
void* ThreadProcessOMXInputPort(void* data);
void* ThreadProcessOMXOutputPort(void* data);

// Function called by the OMX component for event handling
OMX_ERRORTYPE OMXEventHandler(OMX_IN OMX_HANDLETYPE hComponent,
                              OMX_IN OMX_PTR        pAppData,
                              OMX_IN OMX_EVENTTYPE  eEvent,
                              OMX_IN OMX_U32        nData1,
                              OMX_IN OMX_U32        nData2,
                              OMX_IN OMX_PTR        pEventData);

// Function called by the OMX component to indicate the input buffer we passed to it has been consumed
OMX_ERRORTYPE OMXEmptyBufferHandler(OMX_IN OMX_HANDLETYPE        hComponent,
                                    OMX_IN OMX_PTR               pAppData,
                                    OMX_IN OMX_BUFFERHEADERTYPE* pBuffer);

// Function called by the OMX component to hand over the encoded output buffer
OMX_ERRORTYPE OMXFillHandler(OMX_OUT OMX_HANDLETYPE        hComponent,
                             OMX_OUT OMX_PTR               pAppData,
                             OMX_OUT OMX_BUFFERHEADERTYPE* pBuffer);

// Investigate build-time linking to omx instead of this mess
typedef OMX_ERRORTYPE (*OMXGetHandleFunc)(OMX_OUT OMX_HANDLETYPE* handle,
                                          OMX_IN OMX_STRING componentName,
                                          OMX_IN OMX_PTR appData,
                                          OMX_IN OMX_CALLBACKTYPE* callBacks);
typedef OMX_ERRORTYPE (*OMXFreeHandleFunc)(OMX_IN OMX_HANDLETYPE hComp);

static OMX_ERRORTYPE (*OMXInit)(void);
static OMX_ERRORTYPE (*OMXDeinit)(void);
static OMXGetHandleFunc OMXGetHandle;
static OMXFreeHandleFunc OMXFreeHandle;

static void __attribute__((constructor)) setupOMXFuncs()
{
    g_pOmxCoreHandle = dlopen("/usr/lib/libOmxCore.so", RTLD_NOW);

    OMXInit =   (OMX_ERRORTYPE (*)(void))dlsym(g_pOmxCoreHandle, "OMX_Init");
    OMXDeinit = (OMX_ERRORTYPE (*)(void))dlsym(g_pOmxCoreHandle, "OMX_Deinit");
    OMXGetHandle =     (OMXGetHandleFunc)dlsym(g_pOmxCoreHandle, "OMX_GetHandle");
    OMXFreeHandle =   (OMXFreeHandleFunc)dlsym(g_pOmxCoreHandle, "OMX_FreeHandle");
}


// -----------------------------------------------------------------------------------------------------------------------------
// Constructor
// -----------------------------------------------------------------------------------------------------------------------------
VideoEncoder::VideoEncoder(VideoEncoderConfig* pVideoEncoderConfig)
{
    pthread_mutex_init(&out_mutex, NULL);

    pthread_condattr_t attr;
    pthread_condattr_init(&attr);
    pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
    pthread_cond_init(&out_cond, &attr);
    pthread_condattr_destroy(&attr);

    m_VideoEncoderConfig = *pVideoEncoderConfig;
    m_outputPipe        = m_VideoEncoderConfig.outputPipe;
    m_inputBufferSize   = 0;
    m_inputBufferCount  = 0;
    m_outputBufferSize  = 0;
    m_outputBufferCount = 0;

    m_nextInputBufferIndex  = 0;
    m_nextOutputBufferIndex = 0;

    if (OMXInit())
    {
        M_ERROR("OMX Init failed!\n");
        throw -EINVAL;
    }

    if (SetConfig(pVideoEncoderConfig))
    {
        M_ERROR("OMX Set config failed!\n");
        throw -EINVAL;
    }

    if (OMX_SendCommand(m_OMXHandle, OMX_CommandStateSet, (OMX_U32)OMX_StateExecuting, NULL))
    {
        M_ERROR("OMX Set state executing failed!\n");
        throw -EINVAL;
    }

    // We do this so that the OMX component has output buffers to fill the encoded frame data. The output buffers are
    // recycled back to the OMX component once they are returned to us and after we write the frame content to the
    // video file
    for (uint32_t i = 0;  i < m_outputBufferCount; i++)
    {
        if (OMX_FillThisBuffer(m_OMXHandle, m_ppOutputBuffers[i]))
        {
            M_ERROR("OMX Fill buffer: %d failed!\n", i);
            throw -EINVAL;
        }
    }
}

// -----------------------------------------------------------------------------------------------------------------------------
// Destructor
// -----------------------------------------------------------------------------------------------------------------------------
VideoEncoder::~VideoEncoder()
{

    OMXDeinit();

    for (uint32_t i = 0; i < m_inputBufferCount; i++)
    {
        OMX_FreeBuffer(m_OMXHandle, PortIndexIn, m_ppInputBuffers[i]);
        usleep(100000);
    }

    delete m_ppInputBuffers;

    for (uint32_t i = 0; i < m_outputBufferCount; i++)
    {
        OMX_FreeBuffer(m_OMXHandle, PortIndexOut, m_ppOutputBuffers[i]);
        usleep(100000);
    }

    delete m_ppOutputBuffers;

    // if (m_OMXHandle != NULL)
    // {
    //     OMXFreeHandle(m_OMXHandle);
    //     m_OMXHandle = NULL;
    // }

}

// -----------------------------------------------------------------------------------------------------------------------------
// This configures the OMX component i.e. the video encoder's input, output ports and all its parameters and gets it into a
// ready to use state. After this function we can start sending input buffers to the video encoder and it will start sending
// back the encoded frames
// -----------------------------------------------------------------------------------------------------------------------------
OMX_ERRORTYPE VideoEncoder::SetConfig(VideoEncoderConfig* pVideoEncoderConfig)
{
    OMX_CALLBACKTYPE                 callbacks = {OMXEventHandler, OMXEmptyBufferHandler, OMXFillHandler};
    OMX_COLOR_FORMATTYPE             omxFormat = OMX_COLOR_FormatMax;
    OMX_VIDEO_PARAM_PORTFORMATTYPE   videoPortFmt;
    OMX_VIDEO_PARAM_PROFILELEVELTYPE profileLevel;
    int  codingType;
    int  profile;
    int  level;

    m_pHALInputBuffers = pVideoEncoderConfig->inputBuffers;

    OMX_RESET_STRUCT(&videoPortFmt, OMX_VIDEO_PARAM_PORTFORMATTYPE);
    OMX_RESET_STRUCT(&profileLevel, OMX_VIDEO_PARAM_PROFILELEVELTYPE);

    char* pComponentName;

    if (pVideoEncoderConfig->isH265 == true)
    {
        pComponentName = (char *)"OMX.qcom.video.encoder.hevc";
        codingType     = OMX_VIDEO_CodingHEVC;
        profile        = OMX_VIDEO_HEVCProfileMain;
        level          = OMX_VIDEO_HEVCHighTierLevel3;
    }
    else
    {
        pComponentName = (char *)"OMX.qcom.video.encoder.avc";
        codingType     = OMX_VIDEO_CodingAVC;
        profile        = OMX_VIDEO_AVCProfileBaseline;
        level          = OMX_VIDEO_AVCLevel4;
    }

    if (OMXGetHandle(&m_OMXHandle, pComponentName, this, &callbacks))
    {
        M_ERROR("OMX Get handle failed!\n");
        return OMX_ErrorUndefined;
    }

    if (pVideoEncoderConfig->format == HAL_PIXEL_FORMAT_YCbCr_420_888)
    {
        omxFormat = OMX_QCOM_COLOR_FormatYVU420SemiPlanar;
    }
    else
    {
        M_ERROR("OMX Unknown video recording format!\n");
        return OMX_ErrorBadParameter;
    }

    bool isFormatSupported = false;
    // Check if OMX component supports the input frame format
    OMX_S32 index = 0;

    while (!OMX_GetParameter(m_OMXHandle, OMX_IndexParamVideoPortFormat, (OMX_PTR)&videoPortFmt))
    {
        videoPortFmt.nPortIndex = PortIndexIn;
        videoPortFmt.nIndex     = index;

        if (videoPortFmt.eColorFormat == omxFormat)
        {
            isFormatSupported = true;
            break;
        }

        index++;
    }

    if (!isFormatSupported)
    {
        M_ERROR("OMX unsupported video input format: %d\n", videoPortFmt.eColorFormat);
        return OMX_ErrorBadParameter;
    }

    // Configure for H264
    if (codingType == OMX_VIDEO_CodingAVC)
    {
        OMX_VIDEO_PARAM_AVCTYPE avc;

        OMX_RESET_STRUCT(&avc, OMX_VIDEO_PARAM_AVCTYPE);
        avc.nPortIndex = PortIndexOut;

        if (OMX_GetParameter(m_OMXHandle, OMX_IndexParamVideoAvc, (OMX_PTR)&avc))
        {
            M_ERROR("OMX Get parameter of OMX_IndexParamVideoAvc failed\n");
            return OMX_ErrorUndefined;
        }

        avc.nPFrames                  = 29;
        avc.nBFrames                  = 0;
        avc.eProfile                  = (OMX_VIDEO_AVCPROFILETYPE)profile;
        avc.eLevel                    = (OMX_VIDEO_AVCLEVELTYPE)level;
        avc.bUseHadamard              = OMX_FALSE;
        avc.nRefFrames                = 1;
        avc.nRefIdx10ActiveMinus1     = 1;
        avc.nRefIdx11ActiveMinus1     = 0;
        avc.bEnableUEP                = OMX_FALSE;
        avc.bEnableFMO                = OMX_FALSE;
        avc.bEnableASO                = OMX_FALSE;
        avc.bEnableRS                 = OMX_FALSE;
        avc.nAllowedPictureTypes      = 2;
        avc.bFrameMBsOnly             = OMX_FALSE;
        avc.bMBAFF                    = OMX_FALSE;
        avc.bWeightedPPrediction      = OMX_FALSE;
        avc.nWeightedBipredicitonMode = 0;
        avc.bconstIpred               = OMX_FALSE;
        avc.bDirect8x8Inference       = OMX_FALSE;
        avc.bDirectSpatialTemporal    = OMX_FALSE;
        avc.eLoopFilterMode           = OMX_VIDEO_AVCLoopFilterEnable;
        avc.bEntropyCodingCABAC       = OMX_FALSE;
        avc.nCabacInitIdc             = 0;

        OMX_RESET_STRUCT_SIZE_VERSION(&avc, OMX_VIDEO_PARAM_AVCTYPE);

        if (OMX_SetParameter(m_OMXHandle, OMX_IndexParamVideoAvc, (OMX_PTR)&avc))
        {
            M_ERROR("OMX_SetParameter of OMX_IndexParamVideoAvc failed!\n");
            return OMX_ErrorUndefined;
        }
    }
    // Configure for H265
    else if (codingType == OMX_VIDEO_CodingHEVC)
    {
        OMX_VIDEO_PARAM_HEVCTYPE hevc;

        OMX_RESET_STRUCT(&hevc, OMX_VIDEO_PARAM_HEVCTYPE);

        hevc.nPortIndex = PortIndexOut;

        if (OMX_GetParameter(m_OMXHandle, (OMX_INDEXTYPE)OMX_IndexParamVideoHevc, (OMX_PTR)&hevc))
        {
            M_ERROR("OMX_GetParameter of OMX_IndexParamVideoHevc failed!\n");
            return OMX_ErrorUndefined;
        }

        hevc.eProfile = (OMX_VIDEO_HEVCPROFILETYPE)profile;
        hevc.eLevel   = (OMX_VIDEO_HEVCLEVELTYPE)level;

        OMX_RESET_STRUCT_SIZE_VERSION(&hevc, OMX_VIDEO_PARAM_HEVCTYPE);

        if (OMX_SetParameter(m_OMXHandle, (OMX_INDEXTYPE)OMX_IndexParamVideoHevc, (OMX_PTR)&hevc))
        {
            M_ERROR("OMX_SetParameter of OMX_IndexParamVideoHevc failed!\n");
            return OMX_ErrorUndefined;
        }

    }
    else
    {
        M_ERROR("Unsupported coding type!\n");
        return OMX_ErrorBadParameter;
    }

    // Set framerate
    OMX_CONFIG_FRAMERATETYPE framerate;

    OMX_RESET_STRUCT(&framerate, OMX_CONFIG_FRAMERATETYPE);

    framerate.nPortIndex = PortIndexIn;

    if (OMX_GetConfig(m_OMXHandle, OMX_IndexConfigVideoFramerate, (OMX_PTR)&framerate))
    {
        M_ERROR("OMX_GetConfig of OMX_IndexConfigVideoFramerate failed!\n");
        return OMX_ErrorUndefined;
    }
    // FractionToQ16(framerate.xEncodeFramerate, (int)(pVideoEncoderConfig->frameRate * 2), 2);
    framerate.xEncodeFramerate = pVideoEncoderConfig->frameRate;

    OMX_RESET_STRUCT_SIZE_VERSION(&framerate, OMX_CONFIG_FRAMERATETYPE);

    if (OMX_SetConfig(m_OMXHandle, OMX_IndexConfigVideoFramerate, (OMX_PTR)&framerate))
    {
        M_ERROR("OMX_SetConfig of OMX_IndexConfigVideoFramerate failed!\n");
        return OMX_ErrorUndefined;
    }

    // Set Color aspect parameters
    android::DescribeColorAspectsParams colorParams;
    OMX_RESET_STRUCT(&colorParams, android::DescribeColorAspectsParams);
    colorParams.nPortIndex = PortIndexIn;

    if (OMX_GetConfig(m_OMXHandle, (OMX_INDEXTYPE)OMX_QTIIndexConfigDescribeColorAspects, (OMX_PTR)&colorParams))
    {
        M_ERROR("OMX_GetConfig of OMX_QTIIndexConfigDescribeColorAspects failed!\n");
        return OMX_ErrorUndefined;
    }
    colorParams.sAspects.mPrimaries    = android::ColorAspects::PrimariesBT709_5;
    colorParams.sAspects.mTransfer     = android::ColorAspects::TransferSMPTE170M;
    colorParams.sAspects.mMatrixCoeffs = android::ColorAspects::MatrixBT709_5;

    OMX_RESET_STRUCT_SIZE_VERSION(&colorParams, android::DescribeColorAspectsParams);

    if (OMX_SetConfig(m_OMXHandle, (OMX_INDEXTYPE)OMX_QTIIndexConfigDescribeColorAspects, (OMX_PTR)&colorParams))
    {
        M_ERROR("OMX_SetConfig of OMX_QTIIndexConfigDescribeColorAspects failed!\n");
        return OMX_ErrorUndefined;
    }

    // Set Bitrate
    OMX_VIDEO_PARAM_BITRATETYPE paramBitRate;
    OMX_RESET_STRUCT(&paramBitRate, OMX_VIDEO_PARAM_BITRATETYPE);

    paramBitRate.nPortIndex = PortIndexOut;

    if (OMX_GetParameter(m_OMXHandle, (OMX_INDEXTYPE)OMX_IndexParamVideoBitrate, (OMX_PTR)&paramBitRate))
    {
        M_ERROR("OMX_GetParameter of OMX_IndexParamVideoBitrate failed!\n");
        return OMX_ErrorUndefined;
    }
    paramBitRate.eControlRate = OMX_Video_ControlRateVariable;

    if (pVideoEncoderConfig->isBitRateConstant == true)
    {
        paramBitRate.eControlRate = OMX_Video_ControlRateConstant;
    }

    paramBitRate.nTargetBitrate = TargetBitrateDefault;

    if (pVideoEncoderConfig->targetBitRate > 0)
    {
        paramBitRate.nTargetBitrate = pVideoEncoderConfig->targetBitRate;
    }

    OMX_RESET_STRUCT_SIZE_VERSION(&paramBitRate, OMX_VIDEO_PARAM_BITRATETYPE);

    if (OMX_SetParameter(m_OMXHandle, (OMX_INDEXTYPE)OMX_IndexParamVideoBitrate, (OMX_PTR)&paramBitRate))
    {
        M_ERROR("OMX_SetParameter of OMX_IndexParamVideoBitrate failed!\n");
        return OMX_ErrorUndefined;
    }

    // Set/Get input port parameters
    if (SetPortParams((OMX_U32)PortIndexIn,
                      (OMX_U32)(pVideoEncoderConfig->width),
                      (OMX_U32)(pVideoEncoderConfig->height),
                      #ifdef USE_HAL_INPUT_BUFFERS
                      (OMX_U32)(pVideoEncoderConfig->inputBuffers->totalBuffers),
                      #else
                      (OMX_U32)NUM_INPUT_BUFFERS,
                      #endif
                      (OMX_U32)(pVideoEncoderConfig->frameRate),
                      paramBitRate.nTargetBitrate,
                      (OMX_U32*)&m_inputBufferSize,
                      (OMX_U32*)&m_inputBufferCount,
                      omxFormat))
    {
        M_ERROR("OMX SetPortParams of PortIndexIn failed!\n");
        return OMX_ErrorUndefined;
    }

    // Set/Get output port parameters
    if (SetPortParams((OMX_U32)PortIndexOut,
                      (OMX_U32)(pVideoEncoderConfig->width),
                      (OMX_U32)(pVideoEncoderConfig->height),
                      (OMX_U32)NUM_OUTPUT_BUFFERS,
                      (OMX_U32)(pVideoEncoderConfig->frameRate),
                      paramBitRate.nTargetBitrate,
                      (OMX_U32*)&m_outputBufferSize,
                      (OMX_U32*)&m_outputBufferCount,
                      omxFormat))
    {
        M_ERROR("OMX SetPortParams of PortIndexOut failed!\n");
        return OMX_ErrorUndefined;
    }

    // Allocate input / output port buffers
    m_ppInputBuffers  = (OMX_BUFFERHEADERTYPE **)malloc(sizeof(OMX_BUFFERHEADERTYPE *) * m_inputBufferCount);
    m_ppOutputBuffers = (OMX_BUFFERHEADERTYPE **)malloc(sizeof(OMX_BUFFERHEADERTYPE *) * m_outputBufferCount);

    if ((m_ppInputBuffers == NULL) || (m_ppOutputBuffers == NULL))
    {
        M_ERROR("OMX Allocate OMX_BUFFERHEADERTYPE ** failed\n");
        return OMX_ErrorUndefined;
    }

    for (uint32_t i = 0; i < m_inputBufferCount; i++)
    {
        #ifdef USE_HAL_INPUT_BUFFERS
            if(!pVideoEncoderConfig->inputBuffers->bufferBlocks[i].vaddress)
            {
                M_WARN("Encoder expecting(%d) more buffers than module allocated(%d)\n", m_inputBufferCount, i);
                return OMX_ErrorUndefined;
            }
            // The OMX component i.e. the video encoder allocates the block, gets the memory from hal
            if (OMX_UseBuffer (m_OMXHandle, &m_ppInputBuffers[i], PortIndexIn, this, m_inputBufferSize,
                    pVideoEncoderConfig->inputBuffers->bufferBlocks[i].vaddress))
            {
                M_ERROR("OMX_UseBuffer on input buffer: %d failed\n", i);
                return OMX_ErrorUndefined;
            }
        #else
            // The OMX component i.e. the video encoder allocates the memory residing behind these buffers
            if (OMX_AllocateBuffer (m_OMXHandle, &m_ppInputBuffers[i], PortIndexIn, this, m_inputBufferSize))
            {
                M_ERROR("OMX_AllocateBuffer on input buffer: %d failed\n", i);
                return OMX_ErrorUndefined;
            }
        #endif
    }

    for (uint32_t i = 0; i < m_outputBufferCount; i++)
    {
        // The OMX component i.e. the video encoder allocates the memory residing behind these buffers
        if (OMX_AllocateBuffer (m_OMXHandle, &m_ppOutputBuffers[i], PortIndexOut, this, m_outputBufferSize))
        {
            M_ERROR("OMX_AllocateBuffer on output buffer: %d failed\n", i);
            return OMX_ErrorUndefined;
        }
    }

    if (OMX_SendCommand(m_OMXHandle, OMX_CommandStateSet, (OMX_U32)OMX_StateIdle, NULL))
    {
        M_ERROR("------voxl-camera-server ERROR: OMX_SendCommand OMX_StateIdle failed\n");\
        return OMX_ErrorUndefined;
    }

    return OMX_ErrorNone;
}

// -----------------------------------------------------------------------------------------------------------------------------
// This function sets the input or output port parameters and gets the input or output port buffer sizes and count to allocate
// -----------------------------------------------------------------------------------------------------------------------------
OMX_ERRORTYPE VideoEncoder::SetPortParams(OMX_U32  portIndex,               ///< In or Out port
                                          OMX_U32  width,                   ///< Image width
                                          OMX_U32  height,                  ///< Image height
                                          OMX_U32  bufferCountMin,          ///< Minimum number of buffers
                                          OMX_U32  frameRate,               ///< Frame rate
                                          OMX_U32  bitrate,
                                          OMX_U32* pBufferSize,             ///< Returned buffer size
                                          OMX_U32* pBufferCount,            ///< Returned number of buffers
                                          OMX_COLOR_FORMATTYPE inputFormat) ///< Image format on the input port
{

    OMX_PARAM_PORTDEFINITIONTYPE sPortDef;
    OMX_RESET_STRUCT(&sPortDef, OMX_PARAM_PORTDEFINITIONTYPE);

    if ((pBufferSize == NULL) || (pBufferCount == NULL))
    {
        M_ERROR("OMX Buffer error : NULL pointer\n");
        return OMX_ErrorBadParameter;
    }

    sPortDef.nPortIndex = portIndex;

    if (OMX_GetParameter(m_OMXHandle, OMX_IndexParamPortDefinition, (OMX_PTR)&sPortDef))
    {
        M_ERROR("OMX_GetParameter OMX_IndexParamPortDefinition failed!\n");
        return OMX_ErrorUndefined;
    }

    // Get the port buffer count and size
    FractionToQ16(sPortDef.format.video.xFramerate,(int)(frameRate * 2), 2);

    sPortDef.format.video.nFrameWidth  = width;
    sPortDef.format.video.nFrameHeight = height;
    sPortDef.format.video.nBitrate     = bitrate;

    if (portIndex == PortIndexIn)
    {
        sPortDef.format.video.eColorFormat = inputFormat;
    }

    OMX_RESET_STRUCT_SIZE_VERSION(&sPortDef, OMX_PARAM_PORTDEFINITIONTYPE);

    if (OMX_SetParameter(m_OMXHandle, OMX_IndexParamPortDefinition, (OMX_PTR)&sPortDef))
    {
        M_ERROR("OMX_SetParameter OMX_IndexParamPortDefinition failed!\n");
        return OMX_ErrorUndefined;
    }

    // Set the port parameters
    if (bufferCountMin < sPortDef.nBufferCountMin)
    {
        bufferCountMin = sPortDef.nBufferCountMin;
    }

    sPortDef.nBufferCountActual = bufferCountMin;
    // sPortDef.nBufferCountMin    = bufferCountMin;
    M_DEBUG("Buffer Count Expected: %d\n", sPortDef.nBufferCountActual);

    OMX_RESET_STRUCT_SIZE_VERSION(&sPortDef, OMX_PARAM_PORTDEFINITIONTYPE);

    if (OMX_SetParameter(m_OMXHandle, OMX_IndexParamPortDefinition, (OMX_PTR)&sPortDef))
    {
        M_ERROR("OMX_SetParameter OMX_IndexParamPortDefinition failed!\n");
        return OMX_ErrorUndefined;
    }

    if (OMX_GetParameter(m_OMXHandle, OMX_IndexParamPortDefinition, (OMX_PTR)&sPortDef))
    {
        M_ERROR("------voxl-camera-server ERROR: OMX_GetParameter OMX_IndexParamPortDefinition failed!\n");
        return OMX_ErrorUndefined;
    }
    M_DEBUG("Buffer Count Actual: %d\n", sPortDef.nBufferCountActual);

    if(bufferCountMin != sPortDef.nBufferCountActual){
        M_ERROR("Failed to get correct number of buffers from OMX module, expected: %d got: %d\n", bufferCountMin, sPortDef.nBufferCountActual);
        return OMX_ErrorUndefined;
    }

    *pBufferCount = sPortDef.nBufferCountActual;
    *pBufferSize  = sPortDef.nBufferSize;

    return OMX_ErrorNone;
}
// -----------------------------------------------------------------------------------------------------------------------------
// The client calls this interface function to pass in a YUV image frame to be encoded
// -----------------------------------------------------------------------------------------------------------------------------
void VideoEncoder::ProcessFrameToEncode(camera_image_metadata_t meta, BufferBlock* buffer)
{
    pthread_mutex_lock(&out_mutex);
    // Queue up work for thread "ThreadProcessOMXOutputPort"
    out_metaQueue.push_back(meta);
    pthread_cond_signal(&out_cond);
    pthread_mutex_unlock(&out_mutex);

    #ifdef USE_HAL_INPUT_BUFFERS
        OMX_BUFFERHEADERTYPE* OMXBuffer = NULL;
        for(unsigned int i = 0; (OMXBuffer = m_ppInputBuffers[i])->pBuffer != buffer->vaddress; i++) {
            M_VERBOSE("Encoder Buffer Miss\n");
            if(i == m_pHALInputBuffers->totalBuffers - 1){
                M_ERROR("Encoder did not find omx-ready buffer for buffer: 0x%lx, skipping encoding\n", buffer->vaddress);
                return;
            }
        }
        M_VERBOSE("Encoder Buffer Hit\n");
    #else
        OMX_BUFFERHEADERTYPE* OMXBuffer = m_ppInputBuffers[m_nextInputBufferIndex++];

        m_nextInputBufferIndex %= m_inputBufferCount;

        // Copy the YUV frame data into the OMX component input port OMX buffer. The data needs to be provided to the encoder
        // in the way in which it was allocated by gralloc. Gralloc may introduce gaps between the Y and UV data and that's
        // exactly how we have to provide the buffer to the encoder (with the gaps between the Y and UV).
        uint8_t*     pDestAddress = (uint8_t*)OMXBuffer->pBuffer;
        uint8_t*     pSrcAddress  = (uint8_t*)buffer->vaddress;
        memcpy(pDestAddress, pSrcAddress, buffer->size);
    #endif

    OMXBuffer->nFilledLen = buffer->size;
    OMXBuffer->nTimeStamp = meta.timestamp_ns;

    if (OMX_EmptyThisBuffer(m_OMXHandle, OMXBuffer))
    {
        M_ERROR("OMX_EmptyThisBuffer failed for framebuffer: %d\n", meta.frame_id);
    }
    #ifndef USE_HAL_INPUT_BUFFERS
        bufferPushAddress(*m_pHALInputBuffers, buffer->vaddress);
    #endif

}

// -----------------------------------------------------------------------------------------------------------------------------
// This function performs any work necessary to start receiving encoding frames from the client
// -----------------------------------------------------------------------------------------------------------------------------
void VideoEncoder::Start()
{
    pthread_attr_t resultAttr;
    pthread_attr_init(&resultAttr);
    pthread_attr_setdetachstate(&resultAttr, PTHREAD_CREATE_JOINABLE);
    pthread_create(&out_thread,
                   &resultAttr,
                   [](void* data){return ((VideoEncoder*)data)->ThreadProcessOMXOutputPort();},
                   this);

    pthread_attr_destroy(&resultAttr);
}

// -----------------------------------------------------------------------------------------------------------------------------
// This function is called by the client to indicate that no more frames will be sent for encoding
// -----------------------------------------------------------------------------------------------------------------------------
void VideoEncoder::Stop()
{
    stop  = true;
    // The thread wont finish and the "join" call will not return till the last expected encoded frame is received from
    // the encoder OMX component
    pthread_cond_signal(&out_cond);
    pthread_join(out_thread, NULL);

    pthread_mutex_destroy(&out_mutex);
    pthread_cond_destroy(&out_cond);

}

// -----------------------------------------------------------------------------------------------------------------------------
// Function called by the OMX component for event handling
// -----------------------------------------------------------------------------------------------------------------------------
OMX_ERRORTYPE OMXEventHandler(OMX_IN OMX_HANDLETYPE hComponent,     ///< OMX component handle
                              OMX_IN OMX_PTR        pAppData,       ///< Any private app data
                              OMX_IN OMX_EVENTTYPE  eEvent,         ///< Event identifier
                              OMX_IN OMX_U32        nData1,         ///< Data 1
                              OMX_IN OMX_U32        nData2,         ///< Data 2
                              OMX_IN OMX_PTR        pEventData)     ///< Event data
{
    switch (eEvent) {
        case OMX_EventCmdComplete:
            M_DEBUG("OMX Event: OMX_EventCmdComplete\n");
            break;
        case OMX_EventError:
            M_DEBUG("OMX Event: OMX_EventError: ");
            switch ((OMX_ERRORTYPE)nData1) {
                case OMX_ErrorNone:
                    printf("OMX_ErrorNone\n");
                    break;
                case OMX_ErrorInsufficientResources:
                    printf("OMX_ErrorInsufficientResources\n");
                    break;
                case OMX_ErrorUndefined:
                    printf("OMX_ErrorUndefined\n");
                    break;
                case OMX_ErrorInvalidComponentName:
                    printf("OMX_ErrorInvalidComponentName\n");
                    break;
                case OMX_ErrorComponentNotFound:
                    printf("OMX_ErrorComponentNotFound\n");
                    break;
                case OMX_ErrorInvalidComponent:
                    printf("OMX_ErrorInvalidComponent\n");
                    break;
                case OMX_ErrorBadParameter:
                    printf("OMX_ErrorBadParameter\n");
                    break;
                case OMX_ErrorNotImplemented:
                    printf("OMX_ErrorNotImplemented\n");
                    break;
                case OMX_ErrorUnderflow:
                    printf("OMX_ErrorUnderflow\n");
                    break;
                case OMX_ErrorOverflow:
                    printf("OMX_ErrorOverflow\n");
                    break;
                case OMX_ErrorHardware:
                    printf("OMX_ErrorHardware\n");
                    break;
                case OMX_ErrorInvalidState:
                    printf("OMX_ErrorInvalidState\n");
                    break;
                case OMX_ErrorStreamCorrupt:
                    printf("OMX_ErrorStreamCorrupt\n");
                    break;
                case OMX_ErrorPortsNotCompatible:
                    printf("OMX_ErrorPortsNotCompatible\n");
                    break;
                case OMX_ErrorResourcesLost:
                    printf("OMX_ErrorResourcesLost\n");
                    break;
                case OMX_ErrorNoMore:
                    printf("OMX_ErrorNoMore\n");
                    break;
                case OMX_ErrorVersionMismatch:
                    printf("OMX_ErrorVersionMismatch\n");
                    break;
                case OMX_ErrorNotReady:
                    printf("OMX_ErrorNotReady\n");
                    break;
                case OMX_ErrorTimeout:
                    printf("OMX_ErrorTimeout\n");
                    break;
                case OMX_ErrorSameState:
                    printf("OMX_ErrorSameState\n");
                    break;
                case OMX_ErrorResourcesPreempted:
                    printf("OMX_ErrorResourcesPreempted\n");
                    break;
                case OMX_ErrorPortUnresponsiveDuringAllocation:
                    printf("OMX_ErrorPortUnresponsiveDuringAllocation\n");
                    break;
                case OMX_ErrorPortUnresponsiveDuringDeallocation:
                    printf("OMX_ErrorPortUnresponsiveDuringDeallocation\n");
                    break;
                case OMX_ErrorPortUnresponsiveDuringStop:
                    printf("OMX_ErrorPortUnresponsiveDuringStop\n");
                    break;
                case OMX_ErrorIncorrectStateTransition:
                    printf("OMX_ErrorIncorrectStateTransition\n");
                    break;
                case OMX_ErrorIncorrectStateOperation:
                    printf("OMX_ErrorIncorrectStateOperation\n");
                    break;
                case OMX_ErrorUnsupportedSetting:
                    printf("OMX_ErrorUnsupportedSetting\n");
                    break;
                case OMX_ErrorUnsupportedIndex:
                    printf("OMX_ErrorUnsupportedIndex\n");
                    break;
                case OMX_ErrorBadPortIndex:
                    printf("OMX_ErrorBadPortIndex\n");
                    break;
                case OMX_ErrorPortUnpopulated:
                    printf("OMX_ErrorPortUnpopulated\n");
                    break;
                case OMX_ErrorComponentSuspended:
                    printf("OMX_ErrorComponentSuspended\n");
                    break;
                case OMX_ErrorDynamicResourcesUnavailable:
                    printf("OMX_ErrorDynamicResourcesUnavailable\n");
                    break;
                case OMX_ErrorMbErrorsInFrame:
                    printf("OMX_ErrorMbErrorsInFrame\n");
                    break;
                case OMX_ErrorFormatNotDetected:
                    printf("OMX_ErrorFormatNotDetected\n");
                    break;
                case OMX_ErrorContentPipeOpenFailed:
                    printf("OMX_ErrorContentPipeOpenFailed\n");
                    break;
                case OMX_ErrorContentPipeCreationFailed:
                    printf("OMX_ErrorContentPipeCreationFailed\n");
                    break;
                case OMX_ErrorSeperateTablesUsed:
                    printf("OMX_ErrorSeperateTablesUsed\n");
                    break;
                case OMX_ErrorTunnelingUnsupported:
                    printf("OMX_ErrorTunnelingUnsupported\n");
                    break;
                case OMX_ErrorKhronosExtensions:
                    printf("OMX_ErrorKhronosExtensions\n");
                    break;
                case OMX_ErrorVendorStartUnused:
                    printf("OMX_ErrorVendorStartUnused\n");
                    break;
                case OMX_ErrorMax:
                    printf("OMX_ErrorMax\n");
                    break;
            }
            break;
        case OMX_EventMark:
            M_DEBUG("OMX Event: OMX_EventMark\n");
            break;
        case OMX_EventPortSettingsChanged:
            M_DEBUG("OMX Event: OMX_EventPortSettingsChanged\n");
            break;
        case OMX_EventBufferFlag:
            M_DEBUG("OMX Event: OMX_EventBufferFlag\n");
            break;
        case OMX_EventResourcesAcquired:
            M_DEBUG("OMX Event: OMX_EventResourcesAcquired\n");
            break;
        case OMX_EventComponentResumed:
            M_DEBUG("OMX Event: OMX_EventComponentResumed\n");
            break;
        case OMX_EventDynamicResourcesAvailable:
            M_DEBUG("OMX Event: OMX_EventDynamicResourcesAvailable\n");
            break;
        case OMX_EventPortFormatDetected:
            M_DEBUG("OMX Event: OMX_EventPortFormatDetected\n");
            break;
        case OMX_EventKhronosExtensions:
            M_DEBUG("OMX Event: OMX_EventKhronosExtensions\n");
            break;
        case OMX_EventVendorStartUnused:
            M_DEBUG("OMX Event: OMX_EventVendorStartUnused\n");
            break;
        case OMX_EventMax:
            M_DEBUG("OMX Event: OMX_EventMax\n");
            break;
        default:
            M_DEBUG("OMX Event: Unknown\n");
            break;

    }
    return OMX_ErrorNone;
}

// -----------------------------------------------------------------------------------------------------------------------------
// Function called by the OMX component indicating it has completed consuming our YUV frame for encoding
// -----------------------------------------------------------------------------------------------------------------------------
OMX_ERRORTYPE OMXEmptyBufferHandler(OMX_IN OMX_HANDLETYPE        hComponent,    ///< OMX component handle
                                    OMX_IN OMX_PTR               pAppData,      ///< Any private app data
                                    OMX_IN OMX_BUFFERHEADERTYPE* pBuffer)       ///< Buffer that has been emptied
{
    #ifdef USE_HAL_INPUT_BUFFERS
        VideoEncoder*  pVideoEncoder = (VideoEncoder*)pAppData;
        bufferPushAddress(*pVideoEncoder->m_pHALInputBuffers, pBuffer->pBuffer);
    #endif

    return OMX_ErrorNone;
}

// -----------------------------------------------------------------------------------------------------------------------------
// Function called by the OMX component to give us the encoded frame. Since this is a callback we dont do much work but simply
// prepare work that will be done by the worker threads
// -----------------------------------------------------------------------------------------------------------------------------
OMX_ERRORTYPE OMXFillHandler(OMX_OUT OMX_HANDLETYPE        hComponent,  ///< OMX component handle
                             OMX_OUT OMX_PTR               pAppData,    ///< Any private app data
                             OMX_OUT OMX_BUFFERHEADERTYPE* pBuffer)     ///< Buffer that has been filled by OMX component
{

    VideoEncoder*  pVideoEncoder = (VideoEncoder*)pAppData;

    pthread_mutex_lock(&pVideoEncoder->out_mutex);
    // Queue up work for thread "ThreadProcessOMXOutputPort"
    pVideoEncoder->out_msgQueue.push_back(pBuffer);
    pthread_cond_signal(&pVideoEncoder->out_cond);
    pthread_mutex_unlock(&pVideoEncoder->out_mutex);

    return OMX_ErrorNone;
}

// -----------------------------------------------------------------------------------------------------------------------------
// This thread function processes the encoded buffers available on the OMX component's output port
// -----------------------------------------------------------------------------------------------------------------------------
void* VideoEncoder::ThreadProcessOMXOutputPort()
{
    pthread_setname_np(pthread_self(), "omx_out");

    int64_t frameNumber = -1;

    // The condition of the while loop is such that this thread will not terminate till it receives the last expected encoded
    // frame from the OMX component
    while (!stop)
    {
        pthread_mutex_lock(&out_mutex);

        if (out_msgQueue.empty())
        {
            pthread_cond_wait(&out_cond, &out_mutex);
            pthread_mutex_unlock(&out_mutex);
            continue;
        }

        if(out_metaQueue.empty()){
            M_WARN("Trying to process omx output with missing metadata\n");
            pthread_cond_wait(&out_cond, &out_mutex);
            pthread_mutex_unlock(&out_mutex);
            continue;
        }


        // Coming here means we have a encoded frame to process
        OMX_BUFFERHEADERTYPE* pOMXBuffer = out_msgQueue.front();
        out_msgQueue.pop_front();


        camera_image_metadata_t meta     = out_metaQueue.front();
        // 30 bytes is a metadata packet, don't associate it with a frame
        if(pOMXBuffer->nFilledLen != 30){
            out_metaQueue.pop_front();
        } else {
            meta.frame_id = -1;
        }

        pthread_mutex_unlock(&out_mutex);
        frameNumber = meta.frame_id;

        meta.size_bytes = pOMXBuffer->nFilledLen;
        meta.format = m_VideoEncoderConfig.isH265 ? IMAGE_FORMAT_H265 : IMAGE_FORMAT_H264;

        uint8_t *data = pOMXBuffer->pBuffer;
        printf("Frame: %d size: %08X %s\n\t%02X %02X %02X %02X %02X %02X %02X %02X\n\t%02X %02X %02X %02X %02X %02X %02X %02X\n\t%02X %02X %02X %02X %02X %02X %02X %02X\n\t%02X %02X %02X %02X %02X %02X %02X %02X\n\n",
               meta.frame_id,
               meta.size_bytes,
               data[4] == 0x41 ? "" : "---------------", //Indicate non-pframe
               data[0],
               data[1],
               data[2],
               data[3],
               data[4],
               data[5],
               data[6],
               data[7],
               data[8],
               data[9],
               data[10],
               data[11],
               data[12],
               data[13],
               data[14],
               data[15],
               data[16],
               data[17],
               data[18],
               data[19],
               data[20],
               data[21],
               data[22],
               data[23],
               data[24],
               data[25],
               data[26],
               data[27],
               data[28],
               data[29],
               data[30],
               data[31]
               );

        pipe_server_write_camera_frame(m_outputPipe, meta, pOMXBuffer->pBuffer);

        // Since we processed the OMX buffer we can immediately recycle it by sending it to the output port of the OMX
        // component
        if (OMX_FillThisBuffer(m_OMXHandle, pOMXBuffer))
        {
            M_ERROR("OMX_FillThisBuffer resulted in error for frame %d\n", frameNumber);
        }
    }

    M_DEBUG("------ Last frame encoded: %d\n", frameNumber);

    return NULL;
}
