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
#include "hal3_camera.h"
#include "voxl_camera_server.h"
#include <camera/CameraMetadata.h>
#include <camera/VendorTagDescriptor.h>

#define CONTROL_COMMANDS "set_exp_gain,set_exp,set_gain,start_ae,stop_ae"

#define NUM_PREVIEW_BUFFERS 16

using namespace android;

// Main thread functions for request and result processing
static void* ThreadPostProcessResult(void* pData);
void* ThreadIssueCaptureRequests(void* pData);

void controlPipeCallback(int ch, char* string, int bytes, void* context);

// -----------------------------------------------------------------------------------------------------------------------------
// Filled in when the camera module sends result image buffers to us. This gets passed to the capture result handling threads's
// message queue
// -----------------------------------------------------------------------------------------------------------------------------
struct CaptureResultFrameData
{
    // Either preview or video or both may be valid
    BufferBlock* pBufferInfo;            ///< buffer information
    int64_t      timestampNsecs;         ///< Timestamp of the buffer(s) in nano secs
    int          frameNumber;            ///< Frame number associated with the image buffers
    int          gain;                   ///< Gain value
    int64_t      exposureNsecs;          ///< Exposure in nsecs
};

// -----------------------------------------------------------------------------------------------------------------------------
// Constructor
// -----------------------------------------------------------------------------------------------------------------------------
PerCameraMgr::PerCameraMgr(PerCameraInfo pCameraInfo)
{
    m_cameraCallbacks.cameraCallbacks = {&CameraModuleCaptureResult, &CameraModuleNotify};
    m_cameraCallbacks.pPrivate        = this;

    m_requestThread.pCameraMgr = this;
    m_requestThread.stop       = false;
    m_requestThread.EStop      = false;
    m_requestThread.pPrivate   = NULL;
    m_requestThread.msgQueue.clear();

    m_resultThread.pCameraMgr = this;
    m_resultThread.stop       = false;
    m_resultThread.EStop      = false;
    m_resultThread.pPrivate   = NULL;
    m_resultThread.msgQueue.clear();
    m_resultThread.lastResultFrameNumber = -1;

    m_currentExposure = 5259763;
    m_currentGain     = 1000;
    m_nextExposure    = m_currentExposure;
    m_nextGain        = m_currentGain;

    //Always request raw10 frames to make sure we have a buffer for either,
    // post processing thread will figure out what the driver is giving us
    if (pCameraInfo.format == FMT_RAW10 ||
        pCameraInfo.format == FMT_RAW8)
    {
        m_halFmt = HAL_PIXEL_FORMAT_RAW10;
    }
    else if ((pCameraInfo.format == FMT_NV21) ||
             (pCameraInfo.format == FMT_NV12))
    {
        m_halFmt = HAL_PIXEL_FORMAT_YCbCr_420_888;
    }

    m_cameraConfigInfo = pCameraInfo;
    /*
    if(init_exposure_interface(m_cameraId, pCameraInfo.expGainInfo)){
        VOXL_LOG_ERROR("------voxl-camera-server ERROR: Failed to initialize exposure interface!\n");

        throw -EINVAL;
    }*/

    m_isMono = m_cameraConfigInfo.isMono;

    if((m_pCameraModule = HAL3_get_camera_module()) == NULL ){
        VOXL_LOG_ERROR("------voxl-camera-server ERROR: Failed to get HAL module!\n");

        throw -EINVAL;
    }

    m_cameraId = m_cameraConfigInfo.camId;

    if(GetDebugLevel() <= DebugLevel::INFO)
        HAL3_print_camera_resolutions(m_cameraId);

    char cameraName[20];
    sprintf(cameraName, "%d", m_cameraId);

    if (m_pCameraModule->common.methods->open(&m_pCameraModule->common, cameraName, (hw_device_t**)(&m_pDevice)))
    {
        VOXL_LOG_ERROR("------voxl-camera-server ERROR: Open camera %s(%s) failed!\n", cameraName, m_cameraConfigInfo.name);

        throw -EINVAL;
    }

    if (m_pDevice->ops->initialize(m_pDevice, (camera3_callback_ops*)&m_cameraCallbacks))
    {
        VOXL_LOG_ERROR("------voxl-camera-server ERROR: Initialize camera %s(%s) failed!\n", cameraName, m_cameraConfigInfo.name);

        throw -EINVAL;
    }

    if(m_pCameraModule->get_camera_info(m_cameraId, &m_pHalCameraInfo))
    {
        VOXL_LOG_ERROR("------voxl-camera-server ERROR: Get camera %s(%s) info failed!\n", cameraName, m_cameraConfigInfo.name);

        throw -EINVAL;
    }


    if (ConfigureStreams())
    {
        VOXL_LOG_ERROR("ERROR: Failed to configure streams for camera: %s(%s)\n", cameraName, m_cameraConfigInfo.name);

        throw -EINVAL;
    }


    m_pBufferManager = new BufferGroup;

    bufferAllocateBuffers(m_pBufferManager,
                          m_stream->max_buffers,
                          m_stream->width,
                          m_stream->height,
                          m_stream->format,
                          m_stream->usage);

    if(SetupPipes()){
        VOXL_LOG_ERROR("ERROR: Failed to setup pipes for camera: %s(%s)\n", cameraName, m_cameraConfigInfo.name);

        throw -EINVAL;
    }

    // This is the default metadata i.e. camera settings per request. The camera module passes us the best set of baseline
    // settings. We can modify any setting, for any frame or for every frame, as we see fit.
    ConstructDefaultRequestSettings();
}

// -----------------------------------------------------------------------------------------------------------------------------
// Create the streams that we will use to communicate with the camera module
// -----------------------------------------------------------------------------------------------------------------------------
int PerCameraMgr::ConfigureStreams()
{

    // Check if the stream configuration is supported by the camera or not. If cameraid doesnt support the stream configuration
    // we just exit. The stream configuration is checked into the static metadata associated with every camera.
    if (!HAL3_is_config_supported(m_cameraId, GetWidth(), GetHeight(), m_halFmt))
    {
        VOXL_LOG_ERROR("------voxl-camera-server ERROR: Camera %d failed to find supported config!\n", m_cameraId);

        return -EINVAL;
    }

    camera3_stream_configuration_t streamConfig = { 0 };

    m_stream              = new camera3_stream_t();
    m_stream->stream_type = CAMERA3_STREAM_OUTPUT;
    m_stream->width       = GetWidth();
    m_stream->height      = GetHeight();
    m_stream->format      = m_halFmt;
    m_stream->data_space  = HAL_DATASPACE_UNKNOWN;
    m_stream->usage       = GRALLOC_USAGE_HW_COMPOSER | GRALLOC_USAGE_HW_TEXTURE;
    m_stream->rotation    = 0;
    m_stream->max_buffers = NUM_PREVIEW_BUFFERS;
    m_stream->priv        = 0;

    streamConfig.num_streams = 1;
    streamConfig.operation_mode = CAMERA3_STREAM_CONFIGURATION_NORMAL_MODE;
    streamConfig.streams = &m_stream;

    // Call into the camera module to check for support of the required stream config i.e. the required usecase
    if (m_pDevice->ops->configure_streams(m_pDevice, &streamConfig))
    {
        VOXL_LOG_FATAL("voxl-camera-server FATAL: Configure streams failed for camera: %d\n", m_cameraId);
        return -EINVAL;
    }
    VOXL_LOG_ALL("Completed Configure Streams for camera: %d\n", m_cameraId);

    return S_OK;
}

// -----------------------------------------------------------------------------------------------------------------------------
// Construct default camera settings that will be passed to the camera module to be used for capturing the frames
// -----------------------------------------------------------------------------------------------------------------------------
void PerCameraMgr::ConstructDefaultRequestSettings()
{
    //int fpsRange[] = {30, 30};
    int fpsRange[] = {m_cameraConfigInfo.fps, m_cameraConfigInfo.fps};

    int     gainTarget        =  1000;
    int64_t exposureUSecs     =  5259763;
    uint8_t aeMode            =  ANDROID_CONTROL_AE_MODE_ON;
    uint8_t antibanding       =  ANDROID_CONTROL_AE_ANTIBANDING_MODE_AUTO;
    uint8_t afmode            =  ANDROID_CONTROL_AF_MODE_OFF;
    uint8_t faceDetectMode    =  ANDROID_STATISTICS_FACE_DETECT_MODE_OFF;

    // Get the default baseline settings
    camera_metadata_t* pDefaultMetadata =
            (camera_metadata_t *)m_pDevice->ops->construct_default_request_settings(m_pDevice, CAMERA3_TEMPLATE_PREVIEW);

    // Modify all the settings that we want to
    m_requestMetadata = clone_camera_metadata(pDefaultMetadata);

    m_requestMetadata.update(ANDROID_CONTROL_AE_MODE,             &aeMode,         1);
    m_requestMetadata.update(ANDROID_SENSOR_SENSITIVITY,          &gainTarget,     1);
    m_requestMetadata.update(ANDROID_SENSOR_EXPOSURE_TIME,        &exposureUSecs,  1);
    m_requestMetadata.update(ANDROID_STATISTICS_FACE_DETECT_MODE, &faceDetectMode, 1);
    m_requestMetadata.update(ANDROID_CONTROL_AF_MODE,             &(afmode),       1);
    m_requestMetadata.update(ANDROID_CONTROL_AE_ANTIBANDING_MODE, &(antibanding),  1);
    m_requestMetadata.update(ANDROID_CONTROL_AE_TARGET_FPS_RANGE, &fpsRange[0],    2);

}

// -----------------------------------------------------------------------------------------------------------------------------
// This function opens the camera and starts sending the capture requests
// -----------------------------------------------------------------------------------------------------------------------------
void PerCameraMgr::Start()
{

    pthread_condattr_t condAttr;
    pthread_condattr_init(&condAttr);
    pthread_condattr_setclock(&condAttr, CLOCK_MONOTONIC);
    pthread_mutex_init(&m_requestThread.mutex, NULL);
    pthread_mutex_init(&m_resultThread.mutex, NULL);
    pthread_cond_init(&m_requestThread.cond, &condAttr);
    pthread_cond_init(&m_resultThread.cond, &condAttr);
    pthread_condattr_destroy(&condAttr);

    // Start the thread that will process the camera capture result. This thread wont exit till it consumes all expected
    // output buffers from the camera module or it encounters a fatal error
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    pthread_create(&(m_resultThread.thread),  &attr, ThreadPostProcessResult, &m_resultThread);
    pthread_create(&(m_requestThread.thread), &attr, ThreadIssueCaptureRequests, &m_requestThread);
    pthread_attr_destroy(&attr);

}

// -----------------------------------------------------------------------------------------------------------------------------
// This function stops the camera and does all necessary clean up
// -----------------------------------------------------------------------------------------------------------------------------
void PerCameraMgr::Stop()
{

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

    //pthread_cond_signal(&m_resultThread.cond);
    pthread_join(m_resultThread.thread, NULL);
    pthread_cond_signal(&m_resultThread.cond);
    pthread_mutex_unlock(&m_resultThread.mutex);
    pthread_mutex_destroy(&m_resultThread.mutex);
    pthread_cond_destroy(&m_resultThread.cond);

    if (m_pBufferManager != NULL)
    {
        bufferDeleteBuffers(m_pBufferManager);
        delete m_pBufferManager;
        m_pBufferManager = NULL;
    }

    if (m_pDevice != NULL)
    {
        m_pDevice->common.close(&m_pDevice->common);
        m_pDevice = NULL;
    }

    pipe_server_close(m_outputChannel);
}

// -----------------------------------------------------------------------------------------------------------------------------
// Function that will process one capture result sent from the camera module. Remember this function is operating in the camera
// module thread context. So we do the bare minimum work that we need to do and return control back to the camera module. The
// bare minimum we do is to dispatch the work to another worker thread who consumes the image buffers passed to it from here.
// Our result worker thread is "ThreadPostProcessResult(..)"
// -----------------------------------------------------------------------------------------------------------------------------
void PerCameraMgr::ProcessOneCaptureResult(const camera3_capture_result* pHalResult)
{

    if(pHalResult->partial_result > 1){

        VOXL_LOG_ALL("Received metadata for frame %d from camera %s\n", pHalResult->frame_number, GetName());

        int result = 0;
        camera_metadata_ro_entry entry;

        result = find_camera_metadata_ro_entry(pHalResult->result, ANDROID_SENSOR_TIMESTAMP, &entry);

        if ((0 == result) && (entry.count > 0))
        {
            m_currentTimestamp = entry.data.i64[0];
        }

        result = find_camera_metadata_ro_entry(pHalResult->result, ANDROID_SENSOR_SENSITIVITY, &entry);

        if ((0 == result) && (entry.count > 0))
        {
            m_currentGain = entry.data.i32[0];
        }

        result = find_camera_metadata_ro_entry(pHalResult->result, ANDROID_SENSOR_EXPOSURE_TIME, &entry);

        if ((0 == result) && (entry.count > 0))
        {
            m_currentExposure = entry.data.i64[0];
        }
    }

    if (pHalResult->num_output_buffers > 0)
    {

        VOXL_LOG_ALL("Received Frame %d from camera %s\n", pHalResult->frame_number, GetName());

        CaptureResultFrameData* pCaptureResultData = new CaptureResultFrameData;

        pCaptureResultData->frameNumber    = pHalResult->frame_number;
        pCaptureResultData->timestampNsecs = m_currentTimestamp;
        pCaptureResultData->exposureNsecs  = m_currentExposure;
        pCaptureResultData->gain           = m_currentGain;
        pCaptureResultData->pBufferInfo    = bufferGetBufferInfo(m_pBufferManager, pHalResult->output_buffers[0].buffer);

        // Mutex is required for msgQueue access from here and from within the thread wherein it will be de-queued
        pthread_mutex_lock(&m_resultThread.mutex);

        // Queue up work for the result thread "ThreadPostProcessResult"
        m_resultThread.msgQueue.push_back((void*)pCaptureResultData);
        pthread_cond_signal(&m_resultThread.cond);
        pthread_mutex_unlock(&m_resultThread.mutex);

        bufferPush(m_pBufferManager, pHalResult->output_buffers[0].buffer); // This queues up the buffer for recycling

    }

}
// -----------------------------------------------------------------------------------------------------------------------------
// Process the result from the camera module. Essentially handle the metadata and the image buffers that are sent back to us.
// We call the PerCameraMgr class function to handle it so that it can have access to any (non-static)class member data it needs
// Remember this function is operating in the camera module thread context. So we should do the bare minimum work and return.
// -----------------------------------------------------------------------------------------------------------------------------
void PerCameraMgr::CameraModuleCaptureResult(const camera3_callback_ops_t *cb, const camera3_capture_result* pHalResult)
{
    Camera3Callbacks* pCamera3Callbacks = (Camera3Callbacks*)cb;
    PerCameraMgr* pPerCameraMgr = (PerCameraMgr*)pCamera3Callbacks->pPrivate;

    pPerCameraMgr->ProcessOneCaptureResult(pHalResult);
}

// -----------------------------------------------------------------------------------------------------------------------------
// Handle any messages sent to us by the camera module
// -----------------------------------------------------------------------------------------------------------------------------
void PerCameraMgr::CameraModuleNotify(const camera3_callback_ops_t *cb, const camera3_notify_msg_t *msg)
{

    PerCameraMgr* pPerCameraMgr = (PerCameraMgr*)((Camera3Callbacks*)cb)->pPrivate;
    if(pPerCameraMgr->IsStopped()) return;

    if (msg->type == CAMERA3_MSG_ERROR)
    {
        if(msg->message.error.error_code == CAMERA3_MSG_ERROR_DEVICE){


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
    }
}

// -----------------------------------------------------------------------------------------------------------------------------
// Send one capture request to the camera module
// -----------------------------------------------------------------------------------------------------------------------------
int PerCameraMgr::ProcessOneCaptureRequest(int frameNumber)
{
    camera3_capture_request_t request;
    /*
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
    }*/
    //printf("%s, %d\n", __FUNCTION__, __LINE__ );

    std::vector<camera3_stream_buffer_t> streamBufferList;

    camera3_stream_buffer_t streamBuffer;
    streamBuffer.buffer        = (const native_handle_t**)bufferPop(m_pBufferManager);
    streamBuffer.stream        = m_stream;
    streamBuffer.status        = 0;
    streamBuffer.acquire_fence = -1;
    streamBuffer.release_fence = -1;
    streamBufferList.push_back(streamBuffer);

    request.num_output_buffers  = 1;
    request.output_buffers      = streamBufferList.data();
    request.frame_number        = frameNumber;
    request.settings            = m_requestMetadata.getAndLock();
    request.input_buffer        = nullptr;
    //printf("%s, %d\n", __FUNCTION__, __LINE__ );

    /* Return values (from hardware/camera3.h):
     *
     *  0:      On a successful start to processing the capture request
     *
     * -EINVAL: If the input is malformed (the settings are NULL when not
     *          allowed, invalid physical camera settings,
     *          there are 0 output buffers, etc) and capture processing
     *          cannot start. Failures during request processing should be
     *          handled by calling camera3_callback_ops_t.notify(). In case of
     *          this error, the framework will retain responsibility for the
     *          stream buffers' fences and the buffer handles; the HAL should
     *          not close the fences or return these buffers with
     *          process_capture_result.
     *
     * -ENODEV: If the camera device has encountered a serious error. After this
     *          error is returned, only the close() method can be successfully
     *          called by the framework.
     *
     */

    int status = m_pDevice->ops->process_capture_request(m_pDevice, &request);

    //printf("%s, %d\n", __FUNCTION__, __LINE__ );

    if (status)
    {

        //Another thread has already detected the fatal error, return since it has already been handled
        if(IsStopped()) return 0;

        VOXL_LOG_FATAL("\nvoxl-camera-server FATAL: Recieved Fatal error from camera: %s\n",
                     GetName());
        switch (status){
            case -EINVAL :
                VOXL_LOG_ERROR("\tError sending request %d, ErrorCode: -EINVAL\n", frameNumber);
                break;
            case -ENODEV:
                VOXL_LOG_ERROR("\tError sending request %d, ErrorCode: -ENODEV\n", frameNumber);
                break;
            default:
                VOXL_LOG_ERROR("\tError sending request %d, ErrorCode: %d\n", frameNumber, status);
                break;
        }

        EStopCameraServer();
        EStop();
        return -EINVAL;

    }
    m_requestMetadata.unlock(request.settings);

    VOXL_LOG_ALL("Processed request for frame %d for camera %s\n", frameNumber, GetName());

    return S_OK;
}

// -----------------------------------------------------------------------------------------------------------------------------
// Convert 10-bit RAW to 8-bit RAW
// -----------------------------------------------------------------------------------------------------------------------------
void ConvertTo8bitRaw(uint8_t* pSrcPixel, uint8_t* pDestPixel, uint32_t widthPixels, uint32_t heightPixels, uint32_t strideBytes)
{
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
    }
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
{
    // Set thread priority
    pid_t tid = syscall(SYS_gettid);
    int which = PRIO_PROCESS;
    int nice  = -10;

    setpriority(which, tid, nice);

    ///<@todo Pass all the information we obtain using the "GetXXX" functions in "struct ThreadData"
    ThreadData*             pThreadData        = (ThreadData*)pData;
    PerCameraMgr*           pCameraMgr         = pThreadData->pCameraMgr;
    int                     width              = pCameraMgr->GetWidth();
    int                     height             = pCameraMgr->GetHeight();
    uint8_t*                pRaw8bit           = NULL;
    camera_image_metadata_t imageInfo          = { 0 };
    uint8_t*                pSendFrameData     = NULL;
    CameraType              cameraType         = pCameraMgr->GetCameraType();
    //uint32_t                expGainSkipFrames  = pCameraMgr->GetNumSkippedFrames();
    int                     previewframeNumber = 0;
    int                     videoframeNumber   = -1;
    uint8_t*                pStereoLTemp       = NULL;
    uint8_t*                pStereoRTemp       = NULL;
    bool                    is10bit            = false;


    if ((pCameraMgr->GetFormat() == FMT_RAW8))
    {
        pRaw8bit = (uint8_t*)malloc(width * height);
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
        VOXL_LOG_ALL("%s procesing new frame\n", pCameraMgr->GetName());

        CaptureResultFrameData* pCaptureResultData = (CaptureResultFrameData*)pThreadData->msgQueue.front();

        pThreadData->msgQueue.pop_front();
        pthread_mutex_unlock(&pThreadData->mutex);

        BufferBlock* pBufferInfo  = NULL;

        imageInfo.exposure_ns = (int32_t)(pCaptureResultData->exposureNsecs);
        imageInfo.gain        = (float)(pCaptureResultData->gain);

        pSendFrameData = NULL;

        pBufferInfo = pCaptureResultData->pBufferInfo;

        imageInfo.width        = (uint32_t)width;
        imageInfo.height       = (uint32_t)height;
        previewframeNumber     = pCaptureResultData->frameNumber;
        imageInfo.size_bytes   = pBufferInfo->size;
        imageInfo.timestamp_ns = pCaptureResultData->timestampNsecs;
        imageInfo.frame_id     = pCaptureResultData->frameNumber;
        imageInfo.stride       = pBufferInfo->stride;

        if (pBufferInfo->format == HAL_PIXEL_FORMAT_RAW10)
        {

            imageInfo.format     = IMAGE_FORMAT_RAW8;
            imageInfo.size_bytes = (pBufferInfo->width * pBufferInfo->height);
            uint8_t* pSrcPixel   = (uint8_t*)pBufferInfo->vaddress;

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
            imageInfo.format     = IMAGE_FORMAT_NV12;
            pSendFrameData       = (uint8_t*)pBufferInfo->vaddress;
            imageInfo.size_bytes = pBufferInfo->size;

            // For ov7251 camera there is no color so we just send the Y channel data as RAW8
            if (cameraType == CAMTYPE_OV7251)
            {
                imageInfo.format     = IMAGE_FORMAT_RAW8;
                imageInfo.size_bytes = (pBufferInfo->width * pBufferInfo->height);
            }

            // We always send YUV contiguous data out of the camera server
            else {
                bufferMakeYUVContiguous(pBufferInfo);
                ///<@todo assuming 420 format and multiplying by 1.5 because NV21/NV12 is 12 bits per pixel
                imageInfo.size_bytes = (pBufferInfo->width * pBufferInfo->height * 1.5);
            }

        }

        if (pSendFrameData != NULL)
        {
            // Ship the frame out of the camera server
            pipe_server_write_camera_frame(pCameraMgr->m_outputChannel, imageInfo, pSendFrameData);
            VOXL_LOG_ALL("Sent frame %d through pipe %s\n", imageInfo.frame_id, pCameraMgr->GetName());
            /*
            if ((pCaptureResultData->frameNumber % expGainSkipFrames) == 0)
            // ProcessOneCaptureRequest is already checking for GetNumSkippedFrames
            {

                uint32_t    set_exposure_ns;
                int16_t     set_gain;

                if (get_new_exposure(
                        pCameraMgr->getCameraID(),
                        pSendFrameData,
                        width,
                        height,
                        exposureNs,
                        gain,
                        &set_exposure_ns,
                        &set_gain)){
                    VOXL_LOG_ERROR("ERROR Getting new exposure for camera: %s\n", pCameraMgr->GetName());
                } else {
                    exposureNs = set_exposure_ns;
                    gain = set_gain;

                    pCameraMgr->SetNextExpGain(set_exposure_ns, set_gain);
                }
            }*/
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
        VOXL_LOG_INFO("------ Result thread on camera: %s recieved stop command, exiting\n", pCameraMgr->GetName());
    }else if(!pThreadData->EStop){
        VOXL_LOG_INFO("------ Last %s result frame: %d\n", pCameraMgr->GetName(), pThreadData->lastResultFrameNumber);
    }else{
        VOXL_LOG_FATAL("------ voxl-camera-server WARNING: Thread: %s result thread recieved ESTOP\n", pCameraMgr->GetName());
    }

    VOXL_LOG_INFO("Leaving %s result thread\n", pCameraMgr->GetName());

    fflush(stdout);

    return NULL;
}

// -----------------------------------------------------------------------------------------------------------------------------
// Main thread function to initiate the sending of capture requests to the camera module. Keeps on sending the capture requests
// to the camera module till a "stop message" is passed to this thread function
// -----------------------------------------------------------------------------------------------------------------------------
void* ThreadIssueCaptureRequests(void* data)
{
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
        //pthread_mutex_lock(&pThreadData->mutex);
        //if(pCameraMgr->getNumClients() == 0){
        //    pthread_cond_wait(&pThreadData->cond, &pThreadData->mutex);
        //    if(pThreadData->stop || pThreadData->EStop) break;
        //}
        //pthread_mutex_unlock(&pThreadData->mutex);
        pCameraMgr->ProcessOneCaptureRequest(++frame_number);
    }

    // Stop message received. Inform about the last framenumber requested from the camera module. This in turn will be used
    // by the result thread to wait for this frame's image buffers to arrive.
    if(pThreadData->EStop){
        VOXL_LOG_WARNING("------ voxl-camera-server WARNING: Thread: %s request thread recieved ESTOP\n", pCameraMgr->GetName());
    }else{
        pCameraMgr->StoppedSendingRequest(frame_number);
        VOXL_LOG_INFO("------ Last request frame for %s: %d\n", pCameraMgr->GetName(), frame_number);
    }

    VOXL_LOG_INFO("Leaving %s request thread\n", pCameraMgr->GetName());

    fflush(stdout);

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

int PerCameraMgr::SetupPipes(){

    m_outputChannel = pipe_server_get_next_available_channel();

    pipe_info_t info;
    strcpy(info.name       , GetName());
    strcpy(info.type       , "camera_image_metadata_t");
    strcpy(info.server_name, PROCESS_NAME);
    info.size_bytes = 64*1024*1024;
    pipe_server_set_control_cb(m_outputChannel, controlPipeCallback, this);
    pipe_server_set_available_control_commands(m_outputChannel, CONTROL_COMMANDS);

    strcpy(info.location, info.name);
    pipe_server_create(m_outputChannel, info, SERVER_FLAG_EN_CONTROL_PIPE);

    return S_OK;
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

    static constexpr uint16_t MIN_GAIN = 100;
    static constexpr uint16_t MAX_GAIN = 1000;

    static constexpr float MIN_EXP  = 0.02;
    static constexpr float MAX_EXP  = 33;

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
