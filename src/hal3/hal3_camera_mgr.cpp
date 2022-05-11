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
#include <camera/CameraMetadata.h>
#include <camera/VendorTagDescriptor.h>
#include <algorithm>

#include "buffer_manager.h"
#include "common_defs.h"
#include "debug_log.h"
#include "hal3_camera.h"
#include "voxl_camera_server.h"

#define CONTROL_COMMANDS "set_exp_gain,set_exp,set_gain,start_ae,stop_ae"

#define NUM_PREVIEW_BUFFERS 16

#define abs(x,y) ((x) > (y) ? (x) : (y))

#define MAX_STEREO_DISCREPENCY_NS 8000000

using namespace android;

// Main thread functions for request and result processing
static void* ThreadPostProcessResult(void* pData);
void* ThreadIssueCaptureRequests(void* pData);

void controlPipeCallback(int ch, char* string, int bytes, void* context);

// -----------------------------------------------------------------------------------------------------------------------------
// Constructor
// -----------------------------------------------------------------------------------------------------------------------------
PerCameraMgr::PerCameraMgr(PerCameraInfo pCameraInfo) :
    expInterface(pCameraInfo.expGainInfo)
{
    cameraCallbacks.cameraCallbacks = {&CameraModuleCaptureResult, &CameraModuleNotify};
    cameraCallbacks.pPrivate        = this;

    sprintf(name, "%.63s", pCameraInfo.name);
    width  = pCameraInfo.width;
    height = pCameraInfo.height;

    //Always request raw10 frames to make sure we have a buffer for either,
    // post processing thread will figure out what the driver is giving us
    if (pCameraInfo.format == FMT_RAW10 ||
        pCameraInfo.format == FMT_RAW8)
    {
        halFmt = HAL_PIXEL_FORMAT_RAW10;
    }
    else if ((pCameraInfo.format == FMT_NV21) ||
             (pCameraInfo.format == FMT_NV12))
    {
        halFmt = HAL_PIXEL_FORMAT_YCbCr_420_888;
    }

    cameraConfigInfo = pCameraInfo;

    if((pCameraModule = HAL3_get_camera_module()) == NULL ){
        VOXL_LOG_ERROR("ERROR: Failed to get HAL module!\n");

        throw -EINVAL;
    }

    cameraId = cameraConfigInfo.camId;

    if(currentDebugLevel == DebugLevel::VERBOSE)
        HAL3_print_camera_resolutions(cameraId);

    char cameraName[20];
    sprintf(cameraName, "%d", cameraId);

    // Check if the stream configuration is supported by the camera or not. If cameraid doesnt support the stream configuration
    // we just exit. The stream configuration is checked into the static metadata associated with every camera.
    if (!HAL3_is_config_supported(cameraId, width, height, halFmt))
    {
        VOXL_LOG_ERROR("ERROR: Camera %d failed to find supported config: %dx%d\n", cameraId, width, height);

        throw -EINVAL;
    }


    if (pCameraModule->common.methods->open(&pCameraModule->common, cameraName, (hw_device_t**)(&pDevice)))
    {
        VOXL_LOG_ERROR("ERROR: Open camera %s(%s) failed!\n", cameraName, name);

        throw -EINVAL;
    }

    if (pDevice->ops->initialize(pDevice, (camera3_callback_ops*)&cameraCallbacks))
    {
        VOXL_LOG_ERROR("ERROR: Initialize camera %s(%s) failed!\n", cameraName, name);

        throw -EINVAL;
    }

    if (ConfigureStreams())
    {
        VOXL_LOG_ERROR("ERROR: Failed to configure streams for camera: %s(%s)\n", cameraName, name);

        throw -EINVAL;
    }

    if (bufferAllocateBuffers(bufferGroup,
                              NUM_PREVIEW_BUFFERS,
                              stream->width,
                              stream->height,
                              stream->format,
                              stream->usage)) {
        VOXL_LOG_ERROR("ERROR: Failed to allocate buffers for camera: %s(%s)\n", cameraName, name);

        throw -EINVAL;
    }


    // This is the default metadata i.e. camera settings per request. The camera module passes us the best set of baseline
    // settings. We can modify any setting, for any frame or for every frame, as we see fit.
    ConstructDefaultRequestSettings();

    if(cameraConfigInfo.camId2 == -1){
        partnerMode = MODE_MONO;
    } else {
        partnerMode = MODE_STEREO_MASTER;

        PerCameraInfo newInfo = cameraConfigInfo;
        sprintf(newInfo.name, "%s%s", name, "_child");
        newInfo.camId = newInfo.camId2;
        newInfo.camId2 = -1;

        otherMgr = new PerCameraMgr(newInfo);

        otherMgr->setMaster(this);

    }

}

PerCameraMgr::~PerCameraMgr() {
    if (partnerMode == MODE_STEREO_MASTER)
        delete otherMgr;
}


// -----------------------------------------------------------------------------------------------------------------------------
// Create the streams that we will use to communicate with the camera module
// -----------------------------------------------------------------------------------------------------------------------------
int PerCameraMgr::ConfigureStreams()
{

    camera3_stream_configuration_t streamConfig = { 0 };

    stream              = new camera3_stream_t();
    stream->stream_type = CAMERA3_STREAM_OUTPUT;
    stream->width       = width;
    stream->height      = height;
    stream->format      = halFmt;
    stream->data_space  = HAL_DATASPACE_UNKNOWN;
    stream->usage       = GRALLOC_USAGE_HW_COMPOSER | GRALLOC_USAGE_HW_TEXTURE;
    #ifdef APQ8096
    stream->rotation    = CAMERA3_STREAM_ROTATION_0;
    #elif QRB5165
    stream->rotation    = 2;
    #else
    #error "No Platform defined"
    #endif
    stream->max_buffers = NUM_PREVIEW_BUFFERS;
    stream->priv        = 0;

    streamConfig.num_streams = 1;

    #ifdef APQ8096
    streamConfig.operation_mode = QCAMERA3_VENDOR_STREAM_CONFIGURATION_RAW_ONLY_MODE;
    #elif QRB5165
    streamConfig.operation_mode = CAMERA3_STREAM_CONFIGURATION_NORMAL_MODE;
    #else
    #error "No Platform defined"
    #endif

    streamConfig.streams = &stream;

    // Call into the camera module to check for support of the required stream config i.e. the required usecase
    if (pDevice->ops->configure_streams(pDevice, &streamConfig))
    {
        VOXL_LOG_FATAL("voxl-camera-server FATAL: Configure streams failed for camera: %d\n", cameraId);
        return -EINVAL;
    }
    VOXL_LOG_VERBOSE("Completed Configure Streams for camera: %d\n", cameraId);

    return S_OK;
}


// -----------------------------------------------------------------------------------------------------------------------------
// Construct default camera settings that will be passed to the camera module to be used for capturing the frames
// -----------------------------------------------------------------------------------------------------------------------------
void PerCameraMgr::ConstructDefaultRequestSettings()
{

    // Get the default baseline settings
    camera_metadata_t* pDefaultMetadata =
            (camera_metadata_t *)pDevice->ops->construct_default_request_settings(pDevice, CAMERA3_TEMPLATE_PREVIEW);

    // Modify all the settings that we want to
    requestMetadata = clone_camera_metadata(pDefaultMetadata);

    if (cameraConfigInfo.type == CAMTYPE_OV7251 ||
        cameraConfigInfo.type == CAMTYPE_OV7251_PAIR) {

        //This covers the 5 below modes, we want them all off
        uint8_t controlMode = ANDROID_CONTROL_MODE_OFF;
        //uint8_t aeMode            =  ANDROID_CONTROL_AE_MODE_OFF;
        //uint8_t antibanding       =  ANDROID_CONTROL_AE_ANTIBANDING_MODE_OFF;
        //uint8_t afMode            =  ANDROID_CONTROL_AF_MODE_OFF;
        //uint8_t awbMode           =  ANDROID_CONTROL_AWB_MODE_OFF;
        //uint8_t faceDetectMode    =  ANDROID_STATISTICS_FACE_DETECT_MODE_OFF;

        //This covers the 5 below modes, we want them all off
        requestMetadata.update(ANDROID_CONTROL_MODE,                &controlMode,        1);
        //requestMetadata.update(ANDROID_CONTROL_AE_MODE,             &aeMode,             1);
        //requestMetadata.update(ANDROID_STATISTICS_FACE_DETECT_MODE, &faceDetectMode,     1);
        //requestMetadata.update(ANDROID_CONTROL_AF_MODE,             &afMode,             1);
        //requestMetadata.update(ANDROID_CONTROL_AE_ANTIBANDING_MODE, &antibanding,        1);
        //requestMetadata.update(ANDROID_CONTROL_AWB_MODE,            &awbMode,            1);

        setExposure             =  5259763;
        setGain                 =  800;
        usingAE                 =  true;

        requestMetadata.update(ANDROID_SENSOR_EXPOSURE_TIME,        &setExposure,        1);
        requestMetadata.update(ANDROID_SENSOR_SENSITIVITY,          &setGain,            1);

    } else if (cameraConfigInfo.type == CAMTYPE_IMX214){

        //Want these on for hires
        uint8_t aeMode            =  ANDROID_CONTROL_AE_MODE_ON;
        uint8_t antibanding       =  ANDROID_CONTROL_AE_ANTIBANDING_MODE_AUTO;
        uint8_t awbMode           =  ANDROID_CONTROL_AWB_MODE_AUTO;

        // This is the flag for running our AE, want off since we're using ISP's
        usingAE                   =  false;

        //Don't have any autofocus so turn these off
        uint8_t afMode            =  ANDROID_CONTROL_AF_MODE_OFF;
        uint8_t faceDetectMode    =  ANDROID_STATISTICS_FACE_DETECT_MODE_OFF;

        requestMetadata.update(ANDROID_CONTROL_AE_MODE,             &aeMode,             1);
        requestMetadata.update(ANDROID_CONTROL_AE_ANTIBANDING_MODE, &antibanding,        1);
        requestMetadata.update(ANDROID_CONTROL_AWB_MODE,            &awbMode,            1);
        requestMetadata.update(ANDROID_STATISTICS_FACE_DETECT_MODE, &faceDetectMode,     1);
        requestMetadata.update(ANDROID_CONTROL_AF_MODE,             &afMode,             1);

    } else { //Make sure to add the desired parameters for any new cameras

        VOXL_LOG_FATAL("WARNING: Camera %s's type has not been added to %s possible resulting in unknown behavior\n",
            name,
            __FUNCTION__);

    }

    int fpsRange[] = {cameraConfigInfo.fps, cameraConfigInfo.fps};
    int64_t frameDuration = 1e9 / cameraConfigInfo.fps;

    requestMetadata.update(ANDROID_CONTROL_AE_TARGET_FPS_RANGE, &fpsRange[0],        2);
    requestMetadata.update(ANDROID_SENSOR_FRAME_DURATION,       &frameDuration,      1);

}

// -----------------------------------------------------------------------------------------------------------------------------
// This function opens the camera and starts sending the capture requests
// -----------------------------------------------------------------------------------------------------------------------------
void PerCameraMgr::Start()
{

    if(partnerMode != MODE_STEREO_SLAVE){
        if(SetupPipes()){
            VOXL_LOG_ERROR("ERROR: Failed to setup pipes for camera: %s\n", name);

            throw -EINVAL;
        }
    } else {
        outputChannel = otherMgr->outputChannel;
    }

    pthread_condattr_t condAttr;
    pthread_condattr_init(&condAttr);
    pthread_condattr_setclock(&condAttr, CLOCK_MONOTONIC);
    pthread_mutex_init(&requestMutex, NULL);
    pthread_mutex_init(&resultMutex, NULL);
    pthread_mutex_init(&stereoMutex, NULL);
    pthread_cond_init(&requestCond, &condAttr);
    pthread_cond_init(&resultCond, &condAttr);
    pthread_cond_init(&stereoCond, &condAttr);
    pthread_condattr_destroy(&condAttr);

    // Start the thread that will process the camera capture result. This thread wont exit till it consumes all expected
    // output buffers from the camera module or it encounters a fatal error
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    pthread_create(&requestThread, &attr, [](void* data){return ((PerCameraMgr*)data)->ThreadIssueCaptureRequests();}, this);
    pthread_create(&resultThread,  &attr, [](void* data){return ((PerCameraMgr*)data)->ThreadPostProcessResult();},  this);
    pthread_attr_destroy(&attr);

    char buf[16];
    sprintf(buf, "cam%d-request", cameraId);

    pthread_setname_np(requestThread, buf);

    sprintf(buf, "cam%d-result", cameraId);
    pthread_setname_np(resultThread, buf);

    if(partnerMode == MODE_STEREO_MASTER){
        otherMgr->Start();
    }

}

// -----------------------------------------------------------------------------------------------------------------------------
// This function stops the camera and does all necessary clean up
// -----------------------------------------------------------------------------------------------------------------------------
void PerCameraMgr::Stop()
{

    stopped = true;

    if(partnerMode == MODE_STEREO_MASTER){
        otherMgr->stopped = true;
    }

    pthread_cond_signal(&requestCond);
    pthread_join(requestThread, NULL);
    pthread_cond_signal(&requestCond);
    pthread_mutex_unlock(&requestMutex);
    pthread_mutex_destroy(&requestMutex);
    pthread_cond_destroy(&requestCond);

    pthread_cond_signal(&resultCond);
    pthread_join(resultThread, NULL);
    pthread_cond_signal(&resultCond);
    pthread_mutex_unlock(&resultMutex);
    pthread_mutex_destroy(&resultMutex);
    pthread_cond_destroy(&resultCond);

    if(partnerMode == MODE_STEREO_MASTER){
        otherMgr->Stop();
    }

    bufferDeleteBuffers(bufferGroup);

    if (pDevice != NULL)
    {
        pDevice->common.close(&pDevice->common);
        pDevice = NULL;
    }

    pthread_mutex_destroy(&stereoMutex);
    pthread_cond_destroy(&stereoCond);

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

        VOXL_LOG_VERBOSE("Received metadata for frame %d from camera %s\n", pHalResult->frame_number, name);

        int result = 0;
        camera_metadata_ro_entry entry;

        result = find_camera_metadata_ro_entry(pHalResult->result, ANDROID_SENSOR_TIMESTAMP, &entry);

        if (!result && entry.count)
        {
            currentTimestamp = entry.data.i64[0];
        }

        result = find_camera_metadata_ro_entry(pHalResult->result, ANDROID_SENSOR_SENSITIVITY, &entry);

        if (!result && entry.count)
        {
            currentGain = entry.data.i32[0];
        }

        result = find_camera_metadata_ro_entry(pHalResult->result, ANDROID_SENSOR_EXPOSURE_TIME, &entry);

        if (!result && entry.count)
        {
            currentExposure = entry.data.i64[0];
        }
    }

    if (pHalResult->num_output_buffers > 0)
    {

        VOXL_LOG_VERBOSE("Received Frame %d from camera %s\n", pHalResult->frame_number, name);

        currentFrameNumber = pHalResult->frame_number;

        // Mutex is required for msgQueue access from here and from within the thread wherein it will be de-queued
        pthread_mutex_lock(&resultMutex);

        // Queue up work for the result thread "ThreadPostProcessResult"
        resultMsgQueue.push_back(pHalResult->output_buffers[0].buffer);
        pthread_cond_signal(&resultCond);
        pthread_mutex_unlock(&resultMutex);

    }

}

// -----------------------------------------------------------------------------------------------------------------------------
// Process the result from the camera module. Essentially handle the metadata and the image buffers that are sent back to us.
// We call the PerCameraMgr class function to handle it so that it can have access to any (non-static)class member data it needs
// Remember this function is operating in the camera module thread context. So we should do the bare minimum work and return.
// -----------------------------------------------------------------------------------------------------------------------------
void PerCameraMgr::CameraModuleCaptureResult(const camera3_callback_ops_t *cb, const camera3_capture_result* pHalResult)
{
    //printf("recieved notify for: %d\n", pHalResult->frame_number);
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
    if(pPerCameraMgr->stopped) return;

    if (msg->type == CAMERA3_MSG_ERROR)
    {
        if(msg->message.error.error_code == CAMERA3_MSG_ERROR_DEVICE){


            //Another thread has already detected the fatal error, return since it has already been handled
            if(pPerCameraMgr->EStopped) return;

            VOXL_LOG_FATAL("\nvoxl-camera-server FATAL: Recieved Fatal error from camera: %s\n",
                         pPerCameraMgr->name);
            VOXL_LOG_FATAL(  "                          Camera server will be stopped\n");
            EStopCameraServer();

        }else{
            VOXL_LOG_ERROR("voxl-camera-server ERROR: Framenumber: %d ErrorCode: %d\n",
                   msg->message.error.frame_number, msg->message.error.error_code);
        }
    }
}

// -----------------------------------------------------------------------------------------------------------------------------
// Convert 10-bit RAW to 8-bit RAW
// -----------------------------------------------------------------------------------------------------------------------------
static void ConvertTo8bitRaw(uint8_t* pImg, uint32_t widthPixels, uint32_t heightPixels)
{
    // This link has the description of the RAW10 format:
    // https://gitlab.com/SaberMod/pa-android-frameworks-base/commit/d1988a98ed69db8c33b77b5c085ab91d22ef3bbc

    uint32_t *destBuffer = (uint32_t*) pImg;
    // Figure out size of the raw8 destination buffer in 32 bit words
    uint32_t destSize = (widthPixels * heightPixels) / 4;

    for (uint32_t i = 0; i < destSize; i++) {
        *destBuffer++ = *((uint32_t*) pImg);
        // Skip every fifth byte because that is just a collection of the 2
        // least significant bits from the previous four pixels. We don't want
        // those least significant bits.
        pImg += 5;
    }
}

static bool Check10bit(uint8_t* pImg, uint32_t widthPixels, uint32_t heightPixels)
{
    if (pImg == NULL) {
        VOXL_LOG_ERROR("%s was given NULL pointer for image\n", __FUNCTION__);
        throw -EINVAL;
    }

    uint8_t buffer[heightPixels * widthPixels * 2];
    memcpy(buffer, pImg, heightPixels * widthPixels);

    ConvertTo8bitRaw(buffer,
                     widthPixels,
                     heightPixels);
    //check the row that is 4/5ths of the way down the image, if we just converted a
    //raw8 image to raw8, it will be empty
    uint8_t* row = &(buffer[((heightPixels * 4 / 5) + 5) * widthPixels]);
    for(unsigned int i = 0; i < widthPixels; i++){
        if(row[i] != 0){
            return true;
        }
    }
    return false;
}

static void reverse(uint8_t *mem, int size){

    uint8_t buffer;

    for(int i = 0; i < size/2; i++){

        buffer = mem[i];
        mem[i] = mem[size - i];
        mem[size - i] = buffer;

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
void* PerCameraMgr::ThreadPostProcessResult()
{
    char buf[16];
    pthread_getname_np(pthread_self(), buf, 16);
    VOXL_LOG_VERBOSE("Entered thread: %s(tid: %lu)\n", buf, syscall(SYS_gettid));

    // Set thread priority
    pid_t tid = syscall(SYS_gettid);
    int which = PRIO_PROCESS;
    int nice  = -10;

    setpriority(which, tid, nice);

    ///<@todo Pass all the information we obtain using the "GetXXX" functions in "struct ThreadData"
    camera_image_metadata_t imageInfo   = { 0 };
    CameraType              cameraType  = cameraConfigInfo.type;
    bool                    is10bit     = false;

    imageInfo.exposure_ns  = 0;
    imageInfo.gain         = 0.0;
    imageInfo.magic_number = CAMERA_MAGIC_NUMBER;

    // The condition of the while loop is such that this thread will not terminate till it receives the last expected image
    // frame from the camera module or detects the ESTOP flag
    while (!EStopped && lastResultFrameNumber != currentFrameNumber)
    {
        pthread_mutex_lock(&resultMutex);

        if (resultMsgQueue.empty())
        {
            //Wait for a signal that we have recieved a frame or an estop
            pthread_cond_wait(&resultCond, &resultMutex);
        }

        if(EStopped) {
            pthread_mutex_unlock(&resultMutex);
            break;
        }

        if (resultMsgQueue.empty()) {
            pthread_mutex_unlock(&resultMutex);
            continue;
        }

        buffer_handle_t *handle = resultMsgQueue.front();

        // Coming here means we have a result frame to process
        VOXL_LOG_VERBOSE("%s procesing new frame\n", name);

        resultMsgQueue.pop_front();
        pthread_mutex_unlock(&resultMutex);

        if(stopped) {
            bufferPush(bufferGroup, handle);
            pthread_cond_signal(&stereoCond);
            continue;
        }

        BufferBlock* pBufferInfo  = bufferGetBufferInfo(&bufferGroup, handle);

        //imageInfo.exposure_ns = currentExposure;
        //imageInfo.gain        = currentGain;

        //Temporary solution to prevent oscillating until we figure out how to set this to the registers manually
        imageInfo.exposure_ns = setExposure;
        imageInfo.gain        = setGain;

        uint8_t* pSrcPixel     = (uint8_t*)pBufferInfo->vaddress;
        imageInfo.width        = (uint32_t)width;
        imageInfo.height       = (uint32_t)height;
        imageInfo.timestamp_ns = currentTimestamp;
        imageInfo.frame_id     = currentFrameNumber;

        if (halFmt == HAL_PIXEL_FORMAT_RAW10)
        {

            // check the first frame to see if we actually got a raw10 frame or if it's actually raw8
            if(imageInfo.frame_id == 1){

                //Only need to set this info once, put in the condition to save a few cycles
                imageInfo.format     = IMAGE_FORMAT_RAW8;
                imageInfo.size_bytes = width * height;
                imageInfo.stride     = width;

                VOXL_LOG_INFO("Received raw10 frame, checking to see if is actually raw8\n");

                if((is10bit = Check10bit(pSrcPixel, width, height))){
                    VOXL_LOG_INFO("Frame was actually 10 bit, proceeding with conversions\n");
                } else {
                    VOXL_LOG_INFO("Frame was actually 8 bit, sending as is\n");
                }

            }

            if(is10bit){
                ConvertTo8bitRaw(pSrcPixel,
                                 width,
                                 height);
            }
        }
        else
        {
            // For ov7251 camera there is no color so we just send the Y channel data as RAW8
            if (cameraType == CAMTYPE_OV7251)
            {
                imageInfo.format     = IMAGE_FORMAT_RAW8;
                imageInfo.size_bytes = width * height;
            }
            #ifdef APQ8096
            //APQ only, stereo frames can come in as a pair, need to deinterlace them
            else if (cameraType == CAMTYPE_OV7251_PAIR)
            {
                static uint8_t *stereoBuffer = (uint8_t*)malloc(width/2 * height);

                imageInfo.format     = IMAGE_FORMAT_STEREO_RAW8;
                imageInfo.size_bytes = width * height;
                imageInfo.width      = width / 2;

                for(int i = 0; i < height; i++){
                    memcpy(&(pSrcPixel[i * width / 2]), &(pSrcPixel[i * width]), width / 2);
                    memcpy(&(stereoBuffer[i * width / 2]), &(pSrcPixel[(i * width) + (width/2)]), width / 2);
                }
                memcpy(&(pSrcPixel[width/2*height]), stereoBuffer, width/2*height);

            }
            #endif
            // We always send YUV contiguous data out of the camera server
            else {
                imageInfo.format     = IMAGE_FORMAT_NV12;
                bufferMakeYUVContiguous(pBufferInfo);
                ///<@todo assuming 420 format and multiplying by 1.5 because NV21/NV12 is 12 bits per pixel
                imageInfo.size_bytes = (pBufferInfo->width * pBufferInfo->height * 1.5);
            }

            if(cameraConfigInfo.flip){
                if(imageInfo.frame_id == 0){
                    VOXL_LOG_ERROR("Flipping not currently supported for YUV images, writing as-is\n");
                }
                //int ylen = (imageInfo.size_bytes * 2 / 3);
                //int uvlen = (imageInfo.size_bytes / 6);
                //reverse(pSrcPixel, ylen);
                //reverse(pSrcPixel+ylen, uvlen);
                //reverse(pSrcPixel+ylen+uvlen, uvlen);
            }
        }

        if(partnerMode == MODE_MONO){
            // Ship the frame out of the camera server
            pipe_server_write_camera_frame(outputChannel, imageInfo, pSrcPixel);
            VOXL_LOG_VERBOSE("Sent frame %d through pipe %s\n", imageInfo.frame_id, name);

            int64_t    new_exposure_ns;
            int32_t    new_gain;

            if (expInterface.update_exposure(
                    pSrcPixel,
                    width,
                    height,
                    imageInfo.exposure_ns,
                    imageInfo.gain,
                    &new_exposure_ns,
                    &new_gain)){

                setExposure = new_exposure_ns;
                setGain     = new_gain;

            }

        } else if (partnerMode == MODE_STEREO_MASTER){

            switch (imageInfo.format){
                case IMAGE_FORMAT_NV21:
                    imageInfo.format = IMAGE_FORMAT_STEREO_NV21;
                    imageInfo.size_bytes = width * height * 1.5 * 2;
                    break;
                case IMAGE_FORMAT_RAW8:
                    imageInfo.format = IMAGE_FORMAT_STEREO_RAW8;
                    imageInfo.size_bytes = width * height * 2;
                    break;
                case IMAGE_FORMAT_STEREO_RAW8:
                case IMAGE_FORMAT_STEREO_NV21:
                    break;
                default:
                    VOXL_LOG_FATAL("Error: libmodal-pipe does not support stereo pairs in formats other than NV21 or RAW8\n");
                    EStopCameraServer();
                    break;
            }

            NEED_CHILD:
            pthread_mutex_lock(&stereoMutex);
            if(childFrame == NULL){

                struct timespec ts;
                clock_gettime(CLOCK_MONOTONIC, &ts);
                ts.tv_nsec += MAX_STEREO_DISCREPENCY_NS;

                pthread_cond_timedwait(&stereoCond, &stereoMutex, &ts);
            }

            if(EStopped | stopped) {
                pthread_cond_signal(&(otherMgr->stereoCond));
                bufferPush(bufferGroup, handle);
                continue;
            }

            if(childFrame == NULL){
                pthread_mutex_unlock(&stereoMutex);
                VOXL_LOG_INFO("Child frame not received\n");
                bufferPush(bufferGroup, handle);
                continue;
            }

            //Much newer child, discard master but keep the child
            if(childInfo->timestamp_ns - imageInfo.timestamp_ns > MAX_STEREO_DISCREPENCY_NS){
                VOXL_LOG_INFO("INFO: Camera %s recieved much newer child than master (%lu), discarding master and trying again\n", name, childInfo->timestamp_ns - imageInfo.timestamp_ns);
                bufferPush(bufferGroup, handle);
                pthread_mutex_unlock(&stereoMutex);
                continue;
            }

            //Much newer master, discard the child and get a new one
            if(imageInfo.timestamp_ns - childInfo->timestamp_ns > MAX_STEREO_DISCREPENCY_NS){
                VOXL_LOG_INFO("INFO: Camera %s recieved much newer master than child (%lu), discarding child and trying again\n", name, imageInfo.timestamp_ns - childInfo->timestamp_ns);
                childFrame = NULL;
                childInfo  = NULL;
                pthread_mutex_unlock(&stereoMutex);
                pthread_cond_signal(&(otherMgr->stereoCond));
                goto NEED_CHILD;
            }

            // Assume the earlier timestamp is correct
            if(imageInfo.timestamp_ns > childInfo->timestamp_ns){
                imageInfo.timestamp_ns = childInfo->timestamp_ns;
            }

            if(cameraConfigInfo.flip){
                reverse(pSrcPixel, imageInfo.size_bytes/2);
                reverse(childFrame, imageInfo.size_bytes/2);
            }

            // Ship the frame out of the camera server
            pipe_server_write_stereo_frame(outputChannel, imageInfo, pSrcPixel, childFrame);
            VOXL_LOG_VERBOSE("Sent frame %d through pipe %s\n", imageInfo.frame_id, name);

            // Run Auto Exposure
            int64_t    new_exposure_ns;
            int32_t    new_gain;

            if (expInterface.update_exposure(
                    pSrcPixel,
                    width,
                    height,
                    imageInfo.exposure_ns,
                    imageInfo.gain,
                    &new_exposure_ns,
                    &new_gain)){

                setExposure = new_exposure_ns;
                setGain     = new_gain;

                //Pass back the new AE values to the other camera
                otherMgr->setExposure = new_exposure_ns;
                otherMgr->setGain = new_gain;
            }

            //Clear the pointers and signal the child thread for cleanup
            childFrame = NULL;
            childInfo  = NULL;
            pthread_mutex_unlock(&stereoMutex);
            pthread_cond_signal(&(otherMgr->stereoCond));

        } else if (partnerMode == MODE_STEREO_SLAVE){

            pthread_mutex_lock(&(otherMgr->stereoMutex));

            otherMgr->childFrame = pSrcPixel;
            otherMgr->childInfo  = &imageInfo;

            pthread_cond_wait(&stereoCond, &(otherMgr->stereoMutex));
            pthread_mutex_unlock(&(otherMgr->stereoMutex));
        }


        bufferPush(bufferGroup, handle); // This queues up the buffer for recycling

    }

    if(EStopped){
        VOXL_LOG_FATAL("------ voxl-camera-server WARNING: Thread: %s result thread recieved ESTOP\n", name);
    }else if(stopped){
        VOXL_LOG_INFO("------ Result thread on camera: %s recieved stop command, exiting\n", name);
    }else{
        VOXL_LOG_INFO("------ Last %s result frame: %d\n", name, lastResultFrameNumber);
    }

    VOXL_LOG_INFO("Leaving %s result thread\n", name);

    return NULL;
}


// -----------------------------------------------------------------------------------------------------------------------------
// Send one capture request to the camera module
// -----------------------------------------------------------------------------------------------------------------------------
int PerCameraMgr::ProcessOneCaptureRequest(int frameNumber)
{
    camera3_capture_request_t request;

    requestMetadata.update(ANDROID_SENSOR_EXPOSURE_TIME, &setExposure, 1);
    requestMetadata.update(ANDROID_SENSOR_SENSITIVITY,   &setGain, 1);

    // Exposure Debug Oscillator
    /*{
        static int mode = 1;
        static int64_t curexp = 5259763;
        static int64_t curgain = 800;
        const uint8_t aeMode = 0; // Auto exposure is off i.e. the underlying driver does not control exposure/gain

        if(curexp > 5250000) mode = -1;
        if(curexp < 1000000) mode = 1;

        curexp += 50000 * mode;

        requestMetadata.update(ANDROID_CONTROL_AE_MODE, &aeMode, 1);
        requestMetadata.update(ANDROID_SENSOR_EXPOSURE_TIME, &(curexp), 1);
        requestMetadata.update(ANDROID_SENSOR_SENSITIVITY, &(curgain), 1);
    }*/

    std::vector<camera3_stream_buffer_t> streamBufferList;

    camera3_stream_buffer_t streamBuffer;
    streamBuffer.buffer        = (const native_handle_t**)bufferPop(bufferGroup);
    streamBuffer.stream        = stream;
    streamBuffer.status        = 0;
    streamBuffer.acquire_fence = -1;
    streamBuffer.release_fence = -1;

    streamBufferList.push_back(streamBuffer);

    request.num_output_buffers  = 1;
    request.output_buffers      = streamBufferList.data();
    request.frame_number        = frameNumber;
    request.settings            = requestMetadata.getAndLock();
    request.input_buffer        = nullptr;

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

    if (int status = pDevice->ops->process_capture_request(pDevice, &request))
    {

        //Another thread has already detected the fatal error, return since it has already been handled
        if(stopped) return 0;

        VOXL_LOG_FATAL("\nvoxl-camera-server FATAL: Recieved Fatal error from camera: %s\n", name);
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
        return -EINVAL;

    }
    requestMetadata.unlock(request.settings);

    VOXL_LOG_VERBOSE("Processed request for frame %d for camera %s\n", frameNumber, name);

    return S_OK;
}

// -----------------------------------------------------------------------------------------------------------------------------
// Main thread function to initiate the sending of capture requests to the camera module. Keeps on sending the capture requests
// to the camera module till a "stop message" is passed to this thread function
// -----------------------------------------------------------------------------------------------------------------------------
void* PerCameraMgr::ThreadIssueCaptureRequests()
{

    char buf[16];
    pthread_getname_np(pthread_self(), buf, 16);
    VOXL_LOG_VERBOSE("Entered thread: %s(tid: %lu)\n", buf, syscall(SYS_gettid));

    // Set thread priority
    pid_t tid = syscall(SYS_gettid);
    int which = PRIO_PROCESS;
    int nice  = -10;

    int frame_number = 0;

    setpriority(which, tid, nice);

    while (!stopped && !EStopped)
    {
        if(!getNumClients()){
            pthread_cond_wait(&requestCond, &requestMutex);
            if(stopped || EStopped) break;
        }
        ProcessOneCaptureRequest(++frame_number);
    }

    // Stop message received. Inform about the last framenumber requested from the camera module. This in turn will be used
    // by the result thread to wait for this frame's image buffers to arrive.
    if(EStopped){
        VOXL_LOG_WARNING("------ voxl-camera-server WARNING: Thread: %s request thread recieved ESTOP\n", name);
    }else{
        lastResultFrameNumber = frame_number;
        VOXL_LOG_INFO("------ Last request frame for %s: %d\n", name, frame_number);
    }

    VOXL_LOG_INFO("Leaving %s request thread\n", name);

    return NULL;
}

int PerCameraMgr::SetupPipes(){

    outputChannel = pipe_server_get_next_available_channel();

    //Set up the connect callback to be the addClient function (wrapped in a lambda because it's a member function)
    pipe_server_set_connect_cb(
            outputChannel,                                         //Channel
            [](int ch, int client_id, char* name, void* context)   //Callback
                    {((PerCameraMgr*)context)->addClient();},
            this);                                                 //Context

    pipe_info_t info;
    strcpy(info.name       , name);
    strcpy(info.type       , "camera_image_metadata_t");
    strcpy(info.server_name, PROCESS_NAME);
    info.size_bytes = 64*1024*1024;

    strcpy(info.location, info.name);
    pipe_server_create(outputChannel, info, 0);

    return S_OK;
}

void PerCameraMgr::addClient(){

    VOXL_LOG_VERBOSE("Client connected to camera %s\n", name);

    pthread_cond_signal(&requestCond);

    if(partnerMode == MODE_STEREO_MASTER){
        otherMgr->addClient();
    }
}

void PerCameraMgr::EStop(){

    EStopped = true;
    pthread_cond_signal(&requestCond);
    pthread_cond_signal(&resultCond);

    if(partnerMode == MODE_STEREO_MASTER){
        otherMgr->EStop();
    }

}
