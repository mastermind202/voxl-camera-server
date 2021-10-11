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
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <iostream>
#include <modal_pipe.h>
#include <vector>
#include <string>
#include "buffer_manager.h"
#include "common_defs.h"
#include "debug_log.h"
#include "expgain_interface_modalai.h"
#include "hal3_camera.h"
#include "rb5_camera_server.h"

#define MIN_GAIN 100
#define MAX_GAIN 1000

#define MIN_EXP 0.02
#define MAX_EXP 33

extern int              g_outputChannels[CAMTYPE_MAX_TYPES];
// Main thread functions for request and result processing
///<@todo multi-camera
///<@todo need to handle multi-camera of the same type
static void* ThreadPostProcessResult(void* pData); 
///<@todo multi-camera
void* ThreadIssueCaptureRequests(void* pData);

void controlPipeCallback(int ch, char* string, int bytes, void* context);

enum AECommandVals {
    SET_EXP_GAIN,
    SET_EXP,
    SET_GAIN,
    START_AE,
    STOP_AE,
};
static const char* CmdStrings[] = {
    "set_exp_gain",
    "set_exp",
    "set_gain",
    "start_ae",
    "stop_ae"
};


// -----------------------------------------------------------------------------------------------------------------------------
// Filled in when the camera module sends result image buffers to us. This gets passed to the capture result handling threads's
// message queue
// -----------------------------------------------------------------------------------------------------------------------------
struct CaptureResultFrameData
{
    // Either preview or video or both may be valid
    BufferInfo* pPreviewBufferInfo;     ///< Preview buffer information
    BufferInfo* pVideoBufferInfo;       ///< Video buffer information
    BufferInfo* pSnapshotBufferInfo;    ///< Snapshot buffer information
    int64_t     timestampNsecs;         ///< Timestamp of the buffer(s) in nano secs
    int         frameNumber;            ///< Frame number associated with the image buffers
    int         gainTarget;             ///< Gain value
    int64_t     exposureNsecs;          ///< Exposure in nsecs
};

// -----------------------------------------------------------------------------------------------------------------------------
// Constructor
// -----------------------------------------------------------------------------------------------------------------------------
PerCameraMgr::PerCameraMgr()
{/*
    for (uint32_t i = 0; i < StreamTypeMax; i++)
    {
        m_pBufferManager[i] = NULL;
    }

    m_pExpGainIntf             = NULL;
    m_requestThread.pCameraMgr = this;
    m_requestThread.stop       = false;
    m_requestThread.pPrivate   = NULL;
    m_requestThread.msgQueue.clear();

    m_resultThread.pCameraMgr = this;
    m_resultThread.stop       = false;
    m_resultThread.pPrivate   = NULL;
    m_resultThread.msgQueue.clear();
    m_resultThread.lastResultFrameNumber = -1;

    m_currentExposure = 5259763;
    m_currentGain     = 1000;
    m_nextExposure    = m_currentExposure;
    m_nextGain        = m_currentGain;*/
}

// -----------------------------------------------------------------------------------------------------------------------------
// Performs any one time initialization. This function should only be called once.
// -----------------------------------------------------------------------------------------------------------------------------
int PerCameraMgr::Initialize(PerCameraInitData* pPerCameraInitData)
{
    int status = 0;/*
    ///<@todo Add validation logic for the camera info
    const PerCameraInfo* pCameraInfo = pPerCameraInitData->pPerCameraInfo;

    memcpy(&m_cameraConfigInfo, pCameraInfo, sizeof(PerCameraInfo));

    m_pCameraModule       = pPerCameraInitData->pCameraModule;
    m_pHalCameraInfo      = pPerCameraInitData->pHalCameraInfo;
    m_cameraId            = pPerCameraInitData->cameraid;
    m_outputChannel       = g_outputChannels[GetCameraType()];

    m_currentExposure = m_cameraConfigInfo.expGainInfo.exposureNs;
    m_nextExposure    = m_cameraConfigInfo.expGainInfo.exposureNs;
    m_currentGain     = m_cameraConfigInfo.expGainInfo.gain;
    m_nextGain        = m_cameraConfigInfo.expGainInfo.gain;
    pipe_server_set_control_cb(m_outputChannel, controlPipeCallback, this);

    //Always request raw10 frames to make sure we have a buffer for either,
    // post processing thread will figure out what the driver is giving us
    if (pCameraInfo->modeInfo[CAMMODE_PREVIEW].format == PREVIEW_FMT_RAW10 || 
        pCameraInfo->modeInfo[CAMMODE_PREVIEW].format == PREVIEW_FMT_RAW8)
    {
        m_halFmt = HAL_PIXEL_FORMAT_RAW10;
    }
    else if ((pCameraInfo->modeInfo[CAMMODE_PREVIEW].format == PREVIEW_FMT_NV21) ||
             (pCameraInfo->modeInfo[CAMMODE_PREVIEW].format == PREVIEW_FMT_NV12))
    {
        m_halFmt = HAL_PIXEL_FORMAT_YCbCr_420_888;
    }

    if (pCameraInfo->overrideId != -1)
    {
        m_cameraId = pCameraInfo->overrideId;
    }

    // Check if the stream configuration is supported by the camera or not. If cameraid doesnt support the stream configuration
    // we just exit. The stream configuration is checked into the static metadata associated with every camera.
    if (!IsStreamConfigSupported(GetWidth(), GetHeight(), m_halFmt) == true)
    {
        status = -EINVAL;
    }

    char cameraName[20] = {0};
    sprintf(cameraName, "%d", m_cameraId);

    if (status == 0)
    {
        status = m_pCameraModule->common.methods->open(&m_pCameraModule->common, cameraName, (hw_device_t**)(&m_pDevice));

        if (status != 0)
        {
            VOXL_LOG_ERROR("------voxl-camera-server ERROR: Open camera %s failed!\n", cameraName);
        }
    }

    if (status == 0)
    {
        status = m_pDevice->ops->initialize(m_pDevice, (camera3_callback_ops*)&m_cameraCallbacks);

        if (status != 0)
        {
            VOXL_LOG_ERROR("------voxl-camera-server ERROR: Initialize camera %s failed!\n", cameraName);
        }
    }

    if (status == 0)
    {
        // This calls into the camera module and checks if it supports the stream configuration. If it doesnt then we have to
        // bail out.
        status = ConfigureStreams();
    }

    // Since ConfigureStreams is successful lets allocate the buffer memory since we are definitely going to start processing
    // camera frames now
    if (status == 0)
    {
        status = AllocateStreamBuffers();
    }

    if (status == 0)
    {
        // This is the default metadata i.e. camera settings per request. The camera module passes us the best set of baseline
        // settings. We can modify any setting, for any frame or for every frame, as we see fit.
        ConstructDefaultRequestSettings();

        int width  = pCameraInfo->modeInfo[CAMMODE_PREVIEW].width;
        int height = pCameraInfo->modeInfo[CAMMODE_PREVIEW].height;
        int format = pCameraInfo->modeInfo[CAMMODE_PREVIEW].format;

        ///<@todo add stride
        int stride = pCameraInfo->modeInfo[CAMMODE_PREVIEW].width;

        switch (pCameraInfo->expGainInfo.algorithm)
        {
            ExpGainInterfaceData intf_data;
            case CAMAEALGO_MODALAI:

                m_pExpGainIntf = ExpGainInterfaceFactory::Create(CAMAEALGO_MODALAI);
                m_usingAE = true;

                intf_data.width             = width;
                if(GetCameraType() == CAMTYPE_STEREO){
                    intf_data.height        = height/2;
                }else{
                    intf_data.height        = height;
                }
                intf_data.format            = format;
                intf_data.strideInPixels    = stride;
                intf_data.pAlgoSpecificData = &pCameraInfo->expGainInfo.modalai;
                intf_data.cameraType = GetTypeString(GetCameraType());
                m_pExpGainIntf->Initialize(&intf_data);
                break;

            case CAMAEALGO_INVALID:
            case CAMAEALGO_OFF:
            case CAMAEALGO_MAX_TYPES:
            default:
                m_usingAE = false;
                break;
        }
    }
*/
    return status;
}

// -----------------------------------------------------------------------------------------------------------------------------
// Create the streams that we will use to communicate with the camera module
// -----------------------------------------------------------------------------------------------------------------------------
int PerCameraMgr::ConfigureStreams()
{
    int status = 0;/*
    camera3_stream_configuration_t streamConfig = { 0 };
    camera3_stream_t* pStreams[StreamTypeMax]   = { 0 };

    streamConfig.num_streams = 0;

    m_streams[StreamTypePreview].stream_type = CAMERA3_STREAM_OUTPUT;
    m_streams[StreamTypePreview].width       = GetWidth();
    m_streams[StreamTypePreview].height      = GetHeight();
    m_streams[StreamTypePreview].format      = m_halFmt;
	#ifdef USE_GRALLOC1
    m_streams[StreamTypePreview].usage = GRALLOC1_CONSUMER_USAGE_HWCOMPOSER | GRALLOC1_CONSUMER_USAGE_GPU_TEXTURE;
	#else
    if (m_cameraConfigInfo.modeInfo[CAMMODE_PREVIEW].format != PREVIEW_FMT_NV12)
    {
        m_streams[StreamTypePreview].data_space = HAL_DATASPACE_UNKNOWN;
        m_streams[StreamTypePreview].usage      = GRALLOC_USAGE_HW_COMPOSER | GRALLOC_USAGE_HW_TEXTURE;
    }
    else
    {
        m_streams[StreamTypePreview].data_space = HAL_DATASPACE_BT709;
        m_streams[StreamTypePreview].usage      = GRALLOC_USAGE_HW_VIDEO_ENCODER;
    }
	#endif
    m_streams[StreamTypePreview].rotation    = CAMERA3_STREAM_ROTATION_0;
    m_streams[StreamTypePreview].max_buffers = MaxPreviewBuffers;
    m_streams[StreamTypePreview].priv        = 0;

    pStreams[streamConfig.num_streams] = &m_streams[StreamTypePreview];
    streamConfig.num_streams++;
    streamConfig.operation_mode = QCAMERA3_VENDOR_STREAM_CONFIGURATION_RAW_ONLY_MODE;

    streamConfig.streams = &pStreams[0];

    // Call into the camera module to check for support of the required stream config i.e. the required usecase
    status = m_pDevice->ops->configure_streams(m_pDevice, &streamConfig);

    if (status != 0)
    {
        VOXL_LOG_FATAL("voxl-camera-server FATAL: Configure streams failed! Cannot start camera: %d\n", m_cameraId);
    }
*/
    return status;
}

// -----------------------------------------------------------------------------------------------------------------------------
// Function for getting the Jpeg buffer size
// -----------------------------------------------------------------------------------------------------------------------------
int PerCameraMgr::GetJpegBufferSize(uint32_t width, uint32_t height)
{
    //int maxJpegBufferSize = 0;
    int jpegBufferSize    = 0;/*
    // Get max jpeg buffer size
    camera_metadata_ro_entry jpegBufMaxSize;

    camera_metadata_t* pStaticMetadata = (camera_metadata_t *)m_pHalCameraInfo->static_camera_characteristics;

    find_camera_metadata_ro_entry(pStaticMetadata, ANDROID_JPEG_MAX_SIZE, &jpegBufMaxSize);

    if (jpegBufMaxSize.count != 0)
    {
        maxJpegBufferSize = jpegBufMaxSize.data.i32[0];

        // Calculate final jpeg buffer size for the given resolution.
        float scaleFactor  = (1.0f * width * height) / ((maxJpegBufferSize - sizeof(camera3_jpeg_blob))/3.0f);

        jpegBufferSize = scaleFactor * (maxJpegBufferSize - MinJpegBufferSize) + MinJpegBufferSize;
    }
    else
    {
        VOXL_LOG_FATAL("------ voxl-camera-server: can't find maximum JPEG size in static metadata!");
    }*/

    return jpegBufferSize;
}

// -----------------------------------------------------------------------------------------------------------------------------
// Allocate the buffers required per stream. Each stream will have its own BufferManager to manage buffers for that stream
// -----------------------------------------------------------------------------------------------------------------------------
int PerCameraMgr::AllocateStreamBuffers()
{
    int status = 0;
/*
    VOXL_LOG_INFO("------ voxl-camera-server: Preview buffer allocations\n");
    m_pBufferManager[StreamTypePreview] = new BufferManager;

    status = m_pBufferManager[StreamTypePreview]->Initialize(m_streams[StreamTypePreview].max_buffers);

    if (status == 0)
    {
        status = m_pBufferManager[StreamTypePreview]->AllocateBuffers(m_streams[StreamTypePreview].width,
                                                                      m_streams[StreamTypePreview].height,
                                                                      m_streams[StreamTypePreview].format,
                                                                      m_streams[StreamTypePreview].usage,
                                                                      m_streams[StreamTypePreview].usage);
    }
*/
    return status;
}

// -----------------------------------------------------------------------------------------------------------------------------
// Construct default camera settings that will be passed to the camera module to be used for capturing the frames
// -----------------------------------------------------------------------------------------------------------------------------
void PerCameraMgr::ConstructDefaultRequestSettings()
{/*
    int fpsRange[] = {m_cameraConfigInfo.fps, m_cameraConfigInfo.fps};
    camera3_request_template_t type = CAMERA3_TEMPLATE_PREVIEW;

    type = CAMERA3_TEMPLATE_PREVIEW;

    // Get the default baseline settings
    camera_metadata_t* pDefaultMetadata = (camera_metadata_t *)m_pDevice->ops->construct_default_request_settings(m_pDevice,
                                                                                                                  type);

    // Modify all the settings that we want to
    m_requestMetadata = clone_camera_metadata(pDefaultMetadata);
    m_requestMetadata.update(ANDROID_CONTROL_AE_TARGET_FPS_RANGE, &fpsRange[0], 2);

    uint8_t antibanding = ANDROID_CONTROL_AE_ANTIBANDING_MODE_AUTO;
    m_requestMetadata.update(ANDROID_CONTROL_AE_ANTIBANDING_MODE,&(antibanding),sizeof(antibanding));

    uint8_t afmode = ANDROID_CONTROL_AF_MODE_CONTINUOUS_VIDEO;
    m_requestMetadata.update(ANDROID_CONTROL_AF_MODE, &(afmode), 1);

    uint8_t reqFaceDetectMode =  (uint8_t)ANDROID_STATISTICS_FACE_DETECT_MODE_OFF;
    m_requestMetadata.update(ANDROID_STATISTICS_FACE_DETECT_MODE, &reqFaceDetectMode, 1);

    uint8_t aeMode        = 0;
    int     gainTarget    = 1000;
    int64_t exposureNsecs = 5259763;

    if (m_cameraConfigInfo.expGainInfo.algorithm == CAMAEALGO_ISP)
    {
        aeMode = 1;
    }

    gainTarget    = m_cameraConfigInfo.expGainInfo.gain;
    exposureNsecs = m_cameraConfigInfo.expGainInfo.exposureNs;

    m_requestMetadata.update(ANDROID_CONTROL_AE_MODE, &aeMode, 1);
    m_requestMetadata.update(ANDROID_SENSOR_SENSITIVITY, &gainTarget, 1);
    m_requestMetadata.update(ANDROID_SENSOR_EXPOSURE_TIME, &exposureNsecs, 1);
*/
}

// -----------------------------------------------------------------------------------------------------------------------------
// This function opens the camera and starts sending the capture requests
// -----------------------------------------------------------------------------------------------------------------------------
int PerCameraMgr::Start()
{
    int status = 0;
/*
    m_requestThread.pDevice = m_pDevice;
    m_resultThread.pDevice  = m_pDevice;

    pthread_condattr_t attr;
    pthread_condattr_init(&attr);
    pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
    pthread_mutex_init(&m_requestThread.mutex, NULL);
    pthread_mutex_init(&m_resultThread.mutex, NULL);
    pthread_cond_init(&m_requestThread.cond, &attr);
    pthread_cond_init(&m_resultThread.cond, &attr);
    pthread_condattr_destroy(&attr);

    // Start the thread that will process the camera capture result. This thread wont exit till it consumes all expected
    // output buffers from the camera module or it encounters a fatal error
    pthread_attr_t resultAttr;
    pthread_attr_init(&resultAttr);
    pthread_attr_setdetachstate(&resultAttr, PTHREAD_CREATE_JOINABLE);
    pthread_create(&(m_resultThread.thread), &resultAttr, ThreadPostProcessResult, &m_resultThread);
    pthread_attr_destroy(&resultAttr);

    // Start the thread that will send the camera capture request. This thread wont stop issuing requests to the camera
    // module until we terminate the program with Ctrl+C or it encounters a fatal error
    pthread_attr_t requestAttr;
    pthread_attr_init(&requestAttr);
    pthread_attr_setdetachstate(&requestAttr, PTHREAD_CREATE_JOINABLE);
    pthread_create(&(m_requestThread.thread), &requestAttr, ThreadIssueCaptureRequests, &m_requestThread);
    pthread_attr_destroy(&requestAttr);
*/
    return status;
}

// -----------------------------------------------------------------------------------------------------------------------------
// This function stops the camera and does all necessary clean up
// -----------------------------------------------------------------------------------------------------------------------------
void PerCameraMgr::Stop()
{/*
    //Stop has already been called
    if(!m_requestThread.EStop){
        // Not an emergency stop, wait to recieve last frame
        // The result thread will stop when the result of the last frame is received
        m_requestThread.stop = true;
    }

    pthread_cond_signal(&m_requestThread.cond);
    pthread_join(m_requestThread.thread, NULL);
    pthread_cond_signal(&m_requestThread.cond);
    pthread_mutex_unlock(&m_requestThread.mutex);
    pthread_mutex_destroy(&m_requestThread.mutex);
    pthread_cond_destroy(&m_requestThread.cond);

    pthread_cond_signal(&m_resultThread.cond);
    pthread_join(m_resultThread.thread, NULL);
    pthread_cond_signal(&m_resultThread.cond);
    pthread_mutex_unlock(&m_resultThread.mutex);
    pthread_mutex_destroy(&m_resultThread.mutex);
    pthread_cond_destroy(&m_resultThread.cond);

    for (uint32_t i = 0; i < StreamTypeMax; i++)
    {
        if (m_pBufferManager[i] != NULL)
        {
            delete m_pBufferManager[i];
            m_pBufferManager[i] = NULL;
        }
    }

    if (m_pDevice != NULL)
    {
        m_pDevice->common.close(&m_pDevice->common);
        m_pDevice = NULL;
    }

    if (m_pExpGainIntf != NULL)
    {
        delete m_pExpGainIntf;
        m_pExpGainIntf = NULL;
    }
*/
}

// -----------------------------------------------------------------------------------------------------------------------------
// Check if the stream resolution, format is supported in the camera static characteristics
// -----------------------------------------------------------------------------------------------------------------------------
bool PerCameraMgr::IsStreamConfigSupported(int width, int height, int format)
{
    bool isStreamSupported = false;
/*
    if (m_pHalCameraInfo != NULL)
    {
        camera_metadata_t* pStaticMetadata = (camera_metadata_t *)m_pHalCameraInfo->static_camera_characteristics;
        camera_metadata_ro_entry entry;

        // Get the list of all stream resolutions supported and then go through each one of them looking for a match
        int status = find_camera_metadata_ro_entry(pStaticMetadata, ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS, &entry);

        if ((0 == status) && (0 == (entry.count % 4)))
        {
            VOXL_LOG_INFO("Available resolutions for camera: %s:\n", GetTypeString(m_cameraConfigInfo.type));
            for (size_t i = 0; i < entry.count; i+=4)
            {
                if (ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT == entry.data.i32[i + 3])
                {
                    if(format == entry.data.i32[i]){
                        VOXL_LOG_INFO("%d x %d\n", entry.data.i32[i+1], entry.data.i32[i+2]);
                        if ((width  == entry.data.i32[i+1]) &&
                            (height == entry.data.i32[i+2]))
                        {
                            isStreamSupported = true;
                            break;
                        }
                    }
                }
            }
        }
    }

    if (isStreamSupported == false)
    {
        VOXL_LOG_ERROR("Camera Width: %d, Height: %d, Format: %d not supported!\n", 
            width, height, format);
    } else {
        VOXL_LOG_INFO("Resolution: %d x %d Found!\n", width, height);
    }
*/
    return isStreamSupported;
}

// -----------------------------------------------------------------------------------------------------------------------------
// Function that will process one capture result sent from the camera module. Remember this function is operating in the camera
// module thread context. So we do the bare minimum work that we need to do and return control back to the camera module. The
// bare minimum we do is to dispatch the work to another worker thread who consumes the image buffers passed to it from here.
// Our result worker thread is "ThreadPostProcessResult(..)"
// -----------------------------------------------------------------------------------------------------------------------------
void PerCameraMgr::ProcessOneCaptureResult(const camera3_capture_result* pHalResult)
{/*
    int result     = 0;
    int currentIdx = (pHalResult->frame_number % MaxFrameMetadata);
    int nextIdx    = ((currentIdx + 1) % MaxFrameMetadata);

    if (pHalResult->result != NULL)
    {
        camera_metadata_ro_entry entry;

        memset(&m_perFrameMeta[nextIdx], 0, sizeof(m_perFrameMeta[0]));

        result = find_camera_metadata_ro_entry(pHalResult->result, ANDROID_SENSOR_TIMESTAMP, &entry);

        if ((0 == result) && (entry.count > 0))
        {
            m_perFrameMeta[currentIdx].timestampNsecs = entry.data.i64[0];
        }

        result = find_camera_metadata_ro_entry(pHalResult->result, ANDROID_SENSOR_SENSITIVITY, &entry);

        if ((0 == result) && (entry.count > 0))
        {
            m_perFrameMeta[currentIdx].gain = entry.data.i32[0];
        }

        result = find_camera_metadata_ro_entry(pHalResult->result, ANDROID_SENSOR_EXPOSURE_TIME, &entry);

        if ((0 == result) && (entry.count > 0))
        {
            m_perFrameMeta[currentIdx].exposureNsecs = entry.data.i64[0];
        }
    }

    if (pHalResult->num_output_buffers > 0)
    {
        CaptureResultFrameData* pCaptureResultData = new CaptureResultFrameData;

        memset(pCaptureResultData, 0, sizeof(CaptureResultFrameData));

        pCaptureResultData->frameNumber    = pHalResult->frame_number;
        pCaptureResultData->timestampNsecs = m_perFrameMeta[currentIdx].timestampNsecs;

        if (m_perFrameMeta[currentIdx].timestampNsecs == 0)
        {
            FILE* fp = fopen("/data/misc/camera/hal3-camera-ERROR.txt", "a");
            fprintf(fp, "\nBad frame timestamp for frame %d", pHalResult->frame_number);
            fclose(fp);
        }

        if (m_perFrameMeta[currentIdx].exposureNsecs == 0)
        {
            m_perFrameMeta[currentIdx].exposureNsecs = m_currentExposure;
        }

        if (m_perFrameMeta[currentIdx].gain == 0)
        {
            m_perFrameMeta[currentIdx].gain = m_currentGain;
        }

        pCaptureResultData->exposureNsecs = m_perFrameMeta[currentIdx].exposureNsecs;
        pCaptureResultData->gainTarget    = m_perFrameMeta[currentIdx].gain;

        // Go through all the output buffers received. It could be preview only, video only, or preview + video
        for (uint32_t i = 0; i < pHalResult->num_output_buffers; i++)
        {
            buffer_handle_t* pImageBuffer;

            if (pHalResult->output_buffers[i].stream == &m_streams[StreamTypePreview])
            {
                pImageBuffer = pHalResult->output_buffers[i].buffer;
                pCaptureResultData->pPreviewBufferInfo = m_pBufferManager[StreamTypePreview]->GetBufferInfo(pImageBuffer);
                m_pBufferManager[StreamTypePreview]->PutBuffer(pImageBuffer); // This queues up the buffer for recycling
            }
            else if (pHalResult->output_buffers[i].stream == &m_streams[StreamTypeVideo])
            {
                pImageBuffer = pHalResult->output_buffers[i].buffer;
                pCaptureResultData->pVideoBufferInfo = m_pBufferManager[StreamTypeVideo]->GetBufferInfo(pImageBuffer);
                m_pBufferManager[StreamTypeVideo]->PutBuffer(pImageBuffer); // This queues up the buffer for recycling
            }
            else if (pHalResult->output_buffers[i].stream == &m_streams[StreamTypeSnapshot])
            {
                pImageBuffer = pHalResult->output_buffers[i].buffer;
                pCaptureResultData->pSnapshotBufferInfo = m_pBufferManager[StreamTypeSnapshot]->GetBufferInfo(pImageBuffer);
                m_pBufferManager[StreamTypeSnapshot]->PutBuffer(pImageBuffer); // This queues up the buffer for recycling
            }
        }

        // Mutex is required for msgQueue access from here and from within the thread wherein it will be de-queued
        pthread_mutex_lock(&m_resultThread.mutex);
        // Queue up work for the result thread "ThreadPostProcessResult"
        m_resultThread.msgQueue.push_back((void*)pCaptureResultData);
        pthread_cond_signal(&m_resultThread.cond);
        pthread_mutex_unlock(&m_resultThread.mutex);
    }*/
}
// -----------------------------------------------------------------------------------------------------------------------------
// Process the result from the camera module. Essentially handle the metadata and the image buffers that are sent back to us.
// We call the PerCameraMgr class function to handle it so that it can have access to any (non-static)class member data it needs
// Remember this function is operating in the camera module thread context. So we should do the bare minimum work and return.
// -----------------------------------------------------------------------------------------------------------------------------
void PerCameraMgr::CameraModuleCaptureResult(const camera3_callback_ops *cb, const camera3_capture_result* pHalResult)
{
    Camera3Callbacks* pCamera3Callbacks = (Camera3Callbacks*)cb;
    PerCameraMgr* pPerCameraMgr = (PerCameraMgr*)pCamera3Callbacks->pPrivate;

    pPerCameraMgr->ProcessOneCaptureResult(pHalResult);
}

// -----------------------------------------------------------------------------------------------------------------------------
// Handle any messages sent to us by the camera module
// -----------------------------------------------------------------------------------------------------------------------------
void PerCameraMgr::CameraModuleNotify(const struct camera3_callback_ops *cb, const camera3_notify_msg_t *msg)
{/*
    if (msg->type == CAMERA3_MSG_ERROR)
    {
        if(msg->message.error.error_code == CAMERA3_MSG_ERROR_DEVICE){
            Camera3Callbacks* pCamera3Callbacks = (Camera3Callbacks*)cb;
            PerCameraMgr* pPerCameraMgr = (PerCameraMgr*)pCamera3Callbacks->pPrivate;

            //Another thread has already detected the fatal error, return since it has already been handled
            if(pPerCameraMgr->IsStopped()) return;

            VOXL_LOG_FATAL("\nvoxl-camera-server FATAL: Recieved Fatal error from camera: %s\n",
                         pPerCameraMgr->GetName());
            VOXL_LOG_FATAL(  "                          Camera server will be stopped\n");
            EStopCameraServer();

        }else{
            VOXL_LOG_ERROR("voxl-camera-server ERROR: Framenumber: %d ErrorCode: %d\n",
                   msg->message.error.frame_number, msg->message.error.error_code);
        }
    }*/
}

// -----------------------------------------------------------------------------------------------------------------------------
// Send one capture request to the camera module
// -----------------------------------------------------------------------------------------------------------------------------
int PerCameraMgr::ProcessOneCaptureRequest(int frameNumber)
{
    int status = 0;/*
    camera3_capture_request_t request = { 0 };
    camera3_stream_buffer_t   streamBuffers[StreamTypeMax];

    if (m_pExpGainIntf != NULL)
    {
        if ((frameNumber % GetNumSkippedFrames()) == 0 && (m_usingAE || m_nextExposure != -1))
        {
            while (m_nextExposure == -1)
            {
                std::unique_lock<std::mutex> lock(m_expgainCondMutex);
                m_expgainCondVar.wait(lock);
            }

            m_currentExposure = m_nextExposure;
            m_currentGain     = m_nextGain;
            m_nextExposure    = -1;
            m_nextGain        = -1;

            uint8_t aeMode = 0; // Auto exposure is off i.e. the underlying driver does not control exposure/gain
            m_requestMetadata.update(ANDROID_CONTROL_AE_MODE, &aeMode, 1);
            m_requestMetadata.update(ANDROID_SENSOR_EXPOSURE_TIME, &(m_currentExposure), 1);
            m_requestMetadata.update(ANDROID_SENSOR_SENSITIVITY, &(m_currentGain), 1);
        }
    }

    int streamIndex      = 0;
    int numOutputBuffers = 0;

    streamBuffers[streamIndex].buffer        = (const native_handle_t**)(m_pBufferManager[StreamTypePreview]->GetBuffer());
    streamBuffers[streamIndex].stream        = &m_streams[StreamTypePreview];
    streamBuffers[streamIndex].status        = 0;
    streamBuffers[streamIndex].acquire_fence = -1;
    streamBuffers[streamIndex].release_fence = -1;

    request.num_output_buffers = ++numOutputBuffers;
    streamIndex++;

    request.output_buffers = &streamBuffers[0];

    request.frame_number = frameNumber;
    request.settings = m_requestMetadata.getAndLock();
    request.input_buffer = NULL;

    // Call the camera module to send the capture request
    status = m_pDevice->ops->process_capture_request(m_pDevice, &request);

    if (status != 0)
    {

        //Another thread has already detected the fatal error, return since it has already been handled
        if(IsStopped()) return status;

        VOXL_LOG_ERROR("voxl-camera-server ERROR: Error sending request %d, ErrorCode: %d\n", frameNumber, status);
        VOXL_LOG_FATAL("\nvoxl-camera-server FATAL: Recieved Fatal error from camera: %s\n",
                     GetName());
        VOXL_LOG_FATAL(  "                          Camera server will be stopped\n");
        EStopCameraServer();

    }

    m_requestMetadata.unlock(request.settings);
*/
    return status;
}

// -----------------------------------------------------------------------------------------------------------------------------
// Convert 10-bit RAW to 8-bit RAW
// -----------------------------------------------------------------------------------------------------------------------------
void ConvertTo8bitRaw(uint8_t* pSrcPixel, uint8_t* pDestPixel, uint32_t widthPixels, uint32_t heightPixels, uint32_t strideBytes)
{/*
    // This link has the description of the RAW10 format:
    // https://gitlab.com/SaberMod/pa-android-frameworks-base/commit/d1988a98ed69db8c33b77b5c085ab91d22ef3bbc

    uint32_t *destBuffer = (uint32_t*) pDestPixel;
    // Figure out size of the raw8 destination buffer in 32 bit words
    uint32_t destSize = (widthPixels * heightPixels) / 4;

    for (uint32_t i = 0; i < destSize; i++) {
        *destBuffer++ = *((uint32_t*) pSrcPixel);
        // Skip every fifth byte because that is just a collection of the 2
        // least significant bits from the previous four pixels. We don't want
        // those least significant bits.
        pSrcPixel += 5;
    }*/
}

// -----------------------------------------------------------------------------------------------------------------------------
// PerCameraMgr::CameraModuleCaptureResult(..) is the entry callback that is registered with the camera module to be called when
// the camera module has frame result available to be processed by this application. We do not want to do much processing in
// that function since it is being called in the context of the camera module. So we do the bare minimum processing and leave
// the remaining process upto this function. PerCameraMgr::CameraModuleCaptureResult(..) just pushes a message in a queue that
// is monitored by this thread function. This function goes through the message queue and processes all the messages in it. The
// messages are nothing but camera images that this application has received.
// -----------------------------------------------------------------------------------------------------------------------------
void* ThreadPostProcessResult(void* pData)
{/*
    // Set thread priority
    pid_t tid = syscall(SYS_gettid);
    int which = PRIO_PROCESS;
    int nice  = -10;

    setpriority(which, tid, nice);

    ///<@todo Pass all the information we obtain using the "GetXXX" functions in "struct ThreadData"
    ThreadData*             pThreadData        = (ThreadData*)pData;
    PerCameraMgr*           pPerCameraMgr      = pThreadData->pCameraMgr;
    int                     previewWidth       = pPerCameraMgr->GetWidth();
    int                     previewHeight      = pPerCameraMgr->GetHeight();
    uint8_t*                pRaw8bit           = NULL;
    camera_image_metadata_t imageInfo          = { 0 };
    uint8_t*                pSendFrameData     = NULL;
    CameraType              cameraType         = pPerCameraMgr->GetCameraType();
    uint32_t                expGainSkipFrames  = pPerCameraMgr->GetNumSkippedFrames();
    ExpGainInterface*       pExpGainIntf       = pPerCameraMgr->GetExpGainInterface();
    int                     previewframeNumber = 0;
    int                     videoframeNumber   = -1;
    uint8_t*                pStereoLTemp       = NULL;
    uint8_t*                pStereoRTemp       = NULL;
    uint64_t                exposureNs         = pPerCameraMgr->GetCurrentExposure();
    int32_t                 gain               = pPerCameraMgr->GetCurrentGain();
    bool                    is10bit            = false;


    if ((pPerCameraMgr->GetFormat() == PREVIEW_FMT_RAW8))
    {
        pRaw8bit = (uint8_t*)malloc(previewWidth * previewHeight);
    }

    imageInfo.exposure_ns  = 0;
    imageInfo.gain         = 0.0;
    imageInfo.magic_number = CAMERA_MAGIC_NUMBER;

    // The condition of the while loop is such that this thread will not terminate till it receives the last expected image
    // frame from the camera module or detects the ESTOP flag
    while (!pThreadData->EStop && !pThreadData->stop &&
           ((pThreadData->lastResultFrameNumber != previewframeNumber) ||
           ((videoframeNumber != -1) && (pThreadData->lastResultFrameNumber != videoframeNumber))))
    {
        pthread_mutex_lock(&pThreadData->mutex);

        if (pThreadData->msgQueue.empty())
        {
            //Wait for a signal that we have recieved a frame or an estop
            pthread_cond_wait(&pThreadData->cond, &pThreadData->mutex);
        }

        if(pThreadData->EStop || pThreadData->stop) {
            pthread_mutex_unlock(&pThreadData->mutex);
            break;
        }

        // Coming here means we have a result frame to process

        CaptureResultFrameData* pCaptureResultData = (CaptureResultFrameData*)pThreadData->msgQueue.front();

        pThreadData->msgQueue.pop_front();
        pthread_mutex_unlock(&pThreadData->mutex);

        BufferInfo* pBufferInfo  = NULL;

        imageInfo.exposure_ns = (int32_t)(pCaptureResultData->exposureNsecs);
        imageInfo.gain        = (float)(pCaptureResultData->gainTarget);

        pSendFrameData = NULL;

        pBufferInfo = pCaptureResultData->pPreviewBufferInfo;

        imageInfo.width        = (uint32_t)previewWidth;
        imageInfo.height       = (uint32_t)previewHeight;
        previewframeNumber     = pCaptureResultData->frameNumber;
        imageInfo.size_bytes   = pBufferInfo->size;
        imageInfo.timestamp_ns = pCaptureResultData->timestampNsecs;
        imageInfo.frame_id     = pCaptureResultData->frameNumber;
        imageInfo.stride       = pBufferInfo->stride;

        if (pBufferInfo->format == HAL_PIXEL_FORMAT_RAW10)
        {

            imageInfo.format     = IMAGE_FORMAT_RAW8;
            imageInfo.size_bytes = (pBufferInfo->width * pBufferInfo->height);
            uint8_t* pSrcPixel   = (uint8_t*)pBufferInfo->vaddr;
            
            // check the first frame to see if we actually got a raw10 frame or if it's actually raw8
            if(imageInfo.frame_id == 0){
                VOXL_LOG_INFO("Received raw10 frame, checking to see if is actually raw8\n");

                ConvertTo8bitRaw((uint8_t*)pSrcPixel,
                                 (uint8_t*)pRaw8bit,
                                 pBufferInfo->width,
                                 pBufferInfo->height,
                                 pBufferInfo->stride);

                //check the row that is 4/5ths of the way down the image, if we just converted a 
                //raw8 image to raw8, it will be empty
                uint8_t* row = &(pRaw8bit[((imageInfo.height * 4 / 5) + 2) * imageInfo.width]);
                for(unsigned int i = 0; i < pBufferInfo->width; i++){

                    if(row[i] != 0){
                        is10bit = true;
                        break;
                    }

                }

                if(is10bit){
                    VOXL_LOG_INFO("Frame was actually 10 bit, proceeding with conversions\n");
                } else {
                    VOXL_LOG_INFO("Frame was actually 8 bit, sending as is\n");
                }

            }

            if(is10bit){
                pSendFrameData       = pRaw8bit;
                ConvertTo8bitRaw(pSrcPixel,
                                 (uint8_t*)pRaw8bit,
                                 pBufferInfo->width,
                                 pBufferInfo->height,
                                 pBufferInfo->stride);
            } else {
                pSendFrameData = pSrcPixel;
            }

        }
        else 
        {
            ///<@todo need a better way
            ///<@todo need to support sending data of other formats
            imageInfo.format     = IMAGE_FORMAT_NV21;
            pSendFrameData       = (uint8_t*)pBufferInfo->vaddr;
            imageInfo.size_bytes = pBufferInfo->size;

            // For mono camera there is no color information so we just send the Y channel data as RAW8
            if (cameraType == CAMTYPE_TRACKING)
            {
                imageInfo.format     = IMAGE_FORMAT_RAW8;
                imageInfo.size_bytes = (pBufferInfo->width * pBufferInfo->height);
            }
            else if (cameraType == CAMTYPE_STEREO)
            {
                imageInfo.format     = IMAGE_FORMAT_STEREO_RAW8;
                imageInfo.size_bytes = previewWidth * previewHeight * 2;
                imageInfo.width      = (uint32_t)previewWidth;
                imageInfo.stride     = previewWidth;

                //printf("\n size:%d, w:%d h:%d stride:%d", imageInfo.size_bytes, imageInfo.width, imageInfo.height, imageInfo.stride);
            }
            // We always send YUV contiguous data out of the camera server
            else if (pPerCameraMgr->GetPreviewBufferManager()->IsYUVDataContiguous() == 0)
            {
                pSendFrameData       = pPerCameraMgr->GetPreviewBufferManager()->MakeYUVContiguous(pBufferInfo);
                ///<@todo assuming 420 format and multiplying by 1.5 because NV21/NV12 is 12 bits per pixel
                imageInfo.size_bytes = (pBufferInfo->width * pBufferInfo->height * 1.5);
            }

        }

        if (pSendFrameData != NULL)
        {
            // Ship the frame out of the camera server
            pipe_server_write_camera_frame(pPerCameraMgr->m_outputChannel + PREVIEW_CH_OFFSET, imageInfo, pSendFrameData);

            ///<@todo Check if this needs to be its own thread
            if (pExpGainIntf != NULL)
            {
                if( pPerCameraMgr->IsUsingAE()){
                    if ((pCaptureResultData->frameNumber % expGainSkipFrames) == 0)
                    // ProcessOneCaptureRequest is already checking for GetNumSkippedFrames
                    {
                        ExpGainResult    newExpGain;
                        ExpGainFrameData newFrame = { 0 };

                        newFrame.pFramePixels = pSendFrameData;
                        newExpGain.exposure = exposureNs;
                        newExpGain.gain = gain;

                        pExpGainIntf->GetNewExpGain(&newFrame, &newExpGain);

                        exposureNs = newExpGain.exposure;
                        gain = newExpGain.gain;

                        pPerCameraMgr->SetNextExpGain(newExpGain.exposure, newExpGain.gain);
                    }
                } else {
                    exposureNs = pPerCameraMgr->GetCurrentExposure();
                    gain = pPerCameraMgr->GetCurrentGain();
                }
            }
        }

        delete pCaptureResultData;
    }

    if (pRaw8bit != NULL)
    {
        free(pRaw8bit);
    }

    if (pStereoLTemp != NULL)
    {
        free(pStereoLTemp);
    }

    if (pStereoRTemp != NULL)
    {
        free(pStereoRTemp);
    }

    if(pThreadData->stop){
        VOXL_LOG_INFO("------ Result thread on camera: %s recieved stop command, exiting\n", pPerCameraMgr->GetName());
    }else if(!pThreadData->EStop){
        VOXL_LOG_FATAL("------ Last result frame: %d\n", pThreadData->lastResultFrameNumber);
    }else{
        VOXL_LOG_FATAL("------ voxl-camera-server WARNING: Thread: %s result thread recieved ESTOP\n", pPerCameraMgr->GetName());
    }

    fflush(stdout);

*/
    return NULL;
}

// -----------------------------------------------------------------------------------------------------------------------------
// Main thread function to initiate the sending of capture requests to the camera module. Keeps on sending the capture requests
// to the camera module till a "stop message" is passed to this thread function
// -----------------------------------------------------------------------------------------------------------------------------
void* ThreadIssueCaptureRequests(void* data)
{/*
    ThreadData*   pThreadData = (ThreadData*)data;
    PerCameraMgr* pCameraMgr  = pThreadData->pCameraMgr;
    // Set thread priority
    pid_t tid = syscall(SYS_gettid);
    int which = PRIO_PROCESS;
    int nice  = -10;

    int frame_number = -1;

    setpriority(which, tid, nice);

    while (!pThreadData->stop && !pThreadData->EStop)
    {
        pthread_mutex_lock(&pThreadData->mutex);
        if(!force_enable && pCameraMgr->getNumClients() == 0){
            pthread_cond_wait(&pThreadData->cond, &pThreadData->mutex);
            if(pThreadData->stop || pThreadData->EStop) break;
        }
        pthread_mutex_unlock(&pThreadData->mutex);
        pCameraMgr->ProcessOneCaptureRequest(++frame_number);
    }

    // Stop message received. Inform about the last framenumber requested from the camera module. This in turn will be used
    // by the result thread to wait for this frame's image buffers to arrive.
    if(pThreadData->EStop){
        VOXL_LOG_WARNING("------ voxl-camera-server WARNING: Thread: %s request thread recieved ESTOP\n", pCameraMgr->GetName());
    }else if(pThreadData->stop){
        pCameraMgr->StopResultThread();
    }else{
        pCameraMgr->StoppedSendingRequest(frame_number);
        VOXL_LOG_FATAL("------ Last request frame: %d\n", frame_number);
    }*/
    return NULL;
}

// -----------------------------------------------------------------------------------------------------------------------------
// This function is called to indicate that the request sending thread has issued the last request and no more capture requests
// will be sent
// -----------------------------------------------------------------------------------------------------------------------------
void PerCameraMgr::StoppedSendingRequest(int framenumber)
{
    m_resultThread.lastResultFrameNumber = framenumber;
}
// -----------------------------------------------------------------------------------------------------------------------------
// This function is called to indicate that the request sending thread has issued the last request and no more capture requests
// will be sent
// -----------------------------------------------------------------------------------------------------------------------------
void PerCameraMgr::StopResultThread()
{
    m_resultThread.stop = true;
    pthread_cond_signal(&m_resultThread.cond);
}

void PerCameraMgr::addClient(){
    
    pthread_mutex_lock(&m_requestThread.mutex);
    
    pthread_cond_signal(&m_requestThread.cond);

    pthread_mutex_unlock(&m_requestThread.mutex);
}

void PerCameraMgr::EStop(){

    m_requestThread.EStop = true;
    pthread_cond_signal(&m_requestThread.cond);

    m_resultThread.EStop = true;
    pthread_cond_signal(&m_resultThread.cond);
}

void controlPipeCallback(int ch, char* string, int bytes, void* context){


    PerCameraMgr* pCameraMgr  = (PerCameraMgr*) context;

    /**************************
     *
     * SET Exposure and Gain
     *
     */
    if(strncmp(string, CmdStrings[SET_EXP_GAIN], strlen(CmdStrings[SET_EXP_GAIN])) == 0){
       
        char buffer[strlen(CmdStrings[SET_EXP_GAIN])+1];
        float exp = -1.0;
        int gain = -1;

        if(sscanf(string, "%s %f %d", buffer, &exp, &gain) == 3){
            bool valid = true;
            if(exp < MIN_EXP || exp > MAX_EXP){
                valid = false;
                VOXL_LOG_ERROR("Invalid Control Pipe Exposure: %f,\n\tShould be between %f and %f\n", exp, MIN_EXP, MAX_EXP);
            }
            if(gain < MIN_GAIN || gain > MAX_GAIN){
                valid = false;
                VOXL_LOG_ERROR("Invalid Control Pipe Gain: %d,\n\tShould be between %d and %d\n", gain, MIN_GAIN, MAX_GAIN);
            }
            if(valid){
                pCameraMgr->SetUsingAE(false);
                VOXL_LOG_INFO("Camera: %s recieved new exp/gain values: %6.3f(ms) %d\n", pCameraMgr->GetName(), exp, gain);
                pCameraMgr->SetNextExpGain(exp*1000000, gain);
            }
        } else {
            VOXL_LOG_ERROR("Camera: %s failed to get valid exposure/gain values from control pipe\n", pCameraMgr->GetName());
            VOXL_LOG_ERROR("\tShould follow format: \"%s 25 350\"\n", CmdStrings[SET_EXP_GAIN]);
        }

    }
    /**************************
     *
     * SET Exposure 
     *
     */ else if(strncmp(string, CmdStrings[SET_EXP], strlen(CmdStrings[SET_EXP])) == 0){

        char buffer[strlen(CmdStrings[SET_EXP])+1];
        float exp = -1.0;

        if(sscanf(string, "%s %f", buffer, &exp) == 2){
            bool valid = true;
            if(exp < MIN_EXP || exp > MAX_EXP){
                valid = false;
                VOXL_LOG_ERROR("Invalid Control Pipe Exposure: %f,\n\tShould be between %f and %f\n", exp, MIN_EXP, MAX_EXP);
            }
            if(valid){
                pCameraMgr->SetUsingAE(false);
                VOXL_LOG_INFO("Camera: %s recieved new exp value: %6.3f(ms)\n", pCameraMgr->GetName(), exp);
                pCameraMgr->SetNextExpGain(exp*1000000, pCameraMgr->GetCurrentGain());
            }
        } else {
            VOXL_LOG_ERROR("Camera: %s failed to get valid exposure value from control pipe\n", pCameraMgr->GetName());
            VOXL_LOG_ERROR("\tShould follow format: \"%s 25\"\n", CmdStrings[SET_EXP]);
        }
    } 
    /**************************
     *
     * SET Gain
     *
     */ else if(strncmp(string, CmdStrings[SET_GAIN], strlen(CmdStrings[SET_GAIN])) == 0){

        char buffer[strlen(CmdStrings[SET_GAIN])+1];
        int gain = -1;

        if(sscanf(string, "%s %d", buffer, &gain) == 2){
            bool valid = true;
            if(gain < MIN_GAIN || gain > MAX_GAIN){
                valid = false;
                VOXL_LOG_ERROR("Invalid Control Pipe Gain: %d,\n\tShould be between %d and %d\n", gain, MIN_GAIN, MAX_GAIN);
            }
            if(valid){
                pCameraMgr->SetUsingAE(false);
                VOXL_LOG_INFO("Camera: %s recieved new gain value: %d\n", pCameraMgr->GetName(), gain);
                pCameraMgr->SetNextExpGain(pCameraMgr->GetCurrentExposure(), gain);
            }
        } else {
            VOXL_LOG_ERROR("Camera: %s failed to get valid gain value from control pipe\n", pCameraMgr->GetName());
            VOXL_LOG_ERROR("\tShould follow format: \"%s 350\"\n", CmdStrings[SET_GAIN]);
        }
    } 

    /**************************
     *
     * START Auto Exposure 
     *
     */ else if(strncmp(string, CmdStrings[START_AE], strlen(CmdStrings[START_AE])) == 0){
        pCameraMgr->SetUsingAE(true);
        //Use this to awaken the process capture result block and avoid race conditions
        pCameraMgr->SetNextExpGain(pCameraMgr->GetCurrentExposure(), pCameraMgr->GetCurrentGain());
        VOXL_LOG_INFO("Camera: %s starting to use Auto Exposure\n", pCameraMgr->GetName());
    }
    /**************************
     *
     * STOP Auto Exposure 
     *
     */ else if(strncmp(string, CmdStrings[STOP_AE], strlen(CmdStrings[STOP_AE])) == 0){
        if(pCameraMgr->IsUsingAE()){
            VOXL_LOG_INFO("Camera: %s ceasing to use Auto Exposure\n", pCameraMgr->GetName());
            pCameraMgr->SetUsingAE(false);
        }
    } else {
        VOXL_LOG_ERROR("Camera: %s got unknown Command: %s\n", pCameraMgr->GetName(), string);
    }

}