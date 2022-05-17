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
#include <hardware/camera_common.h>
#include <algorithm>

#include "buffer_manager.h"
#include "common_defs.h"
#include "debug_log.h"
#include "hal3_camera.h"
#include "voxl_camera_server.h"

#define CONTROL_COMMANDS "set_exp_gain,set_exp,set_gain,start_ae,stop_ae"

#define NUM_PREVIEW_BUFFERS 16
#define NUM_VIDEO_BUFFERS 16
#define NUM_SNAPSHOT_BUFFERS 6
#define JPEG_DEFUALT_QUALITY        85

#define abs(x,y) ((x) > (y) ? (x) : (y))

#define MAX_STEREO_DISCREPENCY_NS 8000000

using namespace android;

// Platform Specific Flags
#ifdef APQ8096
    #define ROTATION_MODE  CAMERA3_STREAM_ROTATION_0
    #define OPERATION_MODE QCAMERA3_VENDOR_STREAM_CONFIGURATION_RAW_ONLY_MODE
    #define SNAPSHOT_DS    HAL_DATASPACE_JFIF
#elif QRB5165
    #define ROTATION_MODE  2
    #define OPERATION_MODE CAMERA3_STREAM_CONFIGURATION_NORMAL_MODE
    #define SNAPSHOT_DS    HAL_DATASPACE_V0_JFIF
#else
    #error "No Platform defined"
#endif

static const int  minJpegBufferSize = sizeof(camera3_jpeg_blob) + 1024 * 512;

static int estimateJpegBufferSize(camera_metadata_t* cameraCharacteristics, uint32_t width, uint32_t height);
static int32_t HalFmtFromType(int fmt);

void controlPipeCallback(int ch, char* string, int bytes, void* context);

// -----------------------------------------------------------------------------------------------------------------------------
// Constructor
// -----------------------------------------------------------------------------------------------------------------------------
PerCameraMgr::PerCameraMgr(PerCameraInfo pCameraInfo) :
    configInfo    (pCameraInfo),
    outputChannel (pipe_server_get_next_available_channel()),
    cameraId      (pCameraInfo.camId),
    //name          (), // Maybe keep trying to make this work, just use strcpy for now
    en_video      (pCameraInfo.en_video),
    en_snapshot   (pCameraInfo.en_snapshot),
    p_width       (pCameraInfo.p_width),
    p_height      (pCameraInfo.p_height),
    p_halFmt      (HalFmtFromType(pCameraInfo.p_format)),
    v_width       (pCameraInfo.v_width),
    v_height      (pCameraInfo.v_height),
    v_halFmt      (-1),
    s_width       (pCameraInfo.s_width),
    s_height      (pCameraInfo.s_height),
    s_halFmt      (HAL_PIXEL_FORMAT_BLOB),
    pCameraModule (HAL3_get_camera_module()),
    expInterface  (pCameraInfo.expGainInfo),
    usingAE       (pCameraInfo.useAE)
{

    strcpy(name, pCameraInfo.name);

    cameraCallbacks.cameraCallbacks = {&CameraModuleCaptureResult, &CameraModuleNotify};
    cameraCallbacks.pPrivate        = this;

    if(pCameraModule == NULL ){
        VOXL_LOG_ERROR("ERROR: Failed to get HAL module!\n");

        throw -EINVAL;
    }


    if(currentDebugLevel == DebugLevel::VERBOSE)
        HAL3_print_camera_resolutions(cameraId);

    // Check if the stream configuration is supported by the camera or not. If cameraid doesnt support the stream configuration
    // we just exit. The stream configuration is checked into the static metadata associated with every camera.
    if (!HAL3_is_config_supported(cameraId, p_width, p_height, p_halFmt))
    {
        VOXL_LOG_ERROR("ERROR: Camera %d failed to find supported preview config: %dx%d\n", cameraId, p_width, p_height);

        throw -EINVAL;
    }
    if (en_video && !HAL3_is_config_supported(cameraId, v_width, v_height, v_halFmt))
    {
        VOXL_LOG_ERROR("ERROR: Camera %d failed to find supported video config: %dx%d\n", cameraId, v_width, v_height);

        throw -EINVAL;
    }
    if (en_snapshot && !HAL3_is_config_supported(cameraId, s_width, s_height, s_halFmt))
    {
        VOXL_LOG_ERROR("ERROR: Camera %d failed to find supported snapshot config: %dx%d\n", cameraId, s_width, s_height);

        throw -EINVAL;
    }

    char cameraName[20];
    sprintf(cameraName, "%d", cameraId);

    if (pCameraModule->common.methods->open(&pCameraModule->common, cameraName, (hw_device_t**)(&pDevice)))
    {
        VOXL_LOG_ERROR("ERROR: Open camera %s failed!\n", name);

        throw -EINVAL;
    }

    if (pDevice->ops->initialize(pDevice, (camera3_callback_ops*)&cameraCallbacks))
    {
        VOXL_LOG_ERROR("ERROR: Initialize camera %s failed!\n", name);

        throw -EINVAL;
    }

    if (ConfigureStreams())
    {
        VOXL_LOG_ERROR("ERROR: Failed to configure streams for camera: %s\n", name);

        throw -EINVAL;
    }

    if (bufferAllocateBuffers(p_bufferGroup,
                              NUM_PREVIEW_BUFFERS,
                              p_stream.width,
                              p_stream.height,
                              p_stream.format,
                              p_stream.usage)) {
        VOXL_LOG_ERROR("ERROR: Failed to allocate preview buffers for camera: %s\n", name);

        throw -EINVAL;
    }
    if (en_video && bufferAllocateBuffers(v_bufferGroup,
                                          NUM_VIDEO_BUFFERS,
                                          v_stream.width,
                                          v_stream.height,
                                          v_stream.format,
                                          v_stream.usage)) {
        VOXL_LOG_ERROR("ERROR: Failed to allocate video buffers for camera: %s\n", name);

        throw -EINVAL;
    }
    if (en_snapshot) {

        camera_info halCameraInfo;
        pCameraModule->get_camera_info(cameraId, &halCameraInfo);
        camera_metadata_t* pStaticMetadata = (camera_metadata_t *)halCameraInfo.static_camera_characteristics;

        int blobWidth = estimateJpegBufferSize(pStaticMetadata, s_width, s_height);

        if(bufferAllocateBuffers(s_bufferGroup,
                                 NUM_SNAPSHOT_BUFFERS,
                                 blobWidth,
                                 1,
                                 s_stream.format,
                                 s_stream.usage)) {
            VOXL_LOG_ERROR("ERROR: Failed to allocate snapshot buffers for camera: %s\n", name);

            throw -EINVAL;
        }
    }

    if (ConstructDefaultRequestSettings()){

        VOXL_LOG_ERROR("ERROR: Failed to construct request settings for camera: %s\n", name);

        throw -EINVAL;
    }

    if(configInfo.camId2 == -1){
        partnerMode = MODE_MONO;
    } else {
        partnerMode = MODE_STEREO_MASTER;

        PerCameraInfo newInfo = configInfo;
        sprintf(newInfo.name, "%s%s", name, "_child");
        newInfo.camId = newInfo.camId2;
        newInfo.camId2 = -1;

        // These are disabled until(if) we figure out a good way to handle them
        newInfo.en_video = false;
        newInfo.en_snapshot = false;

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

    std::vector<camera3_stream_t*> streams;

    camera3_stream_configuration_t streamConfig = { 0 };

    streamConfig.operation_mode = OPERATION_MODE;
    streamConfig.num_streams    = 0;

    p_stream.stream_type = CAMERA3_STREAM_OUTPUT;
    p_stream.width       = p_width;
    p_stream.height      = p_height;
    p_stream.format      = p_halFmt;
    p_stream.data_space  = HAL_DATASPACE_UNKNOWN;
    p_stream.usage       = GRALLOC_USAGE_HW_COMPOSER | GRALLOC_USAGE_HW_TEXTURE;
    p_stream.rotation    = ROTATION_MODE;
    p_stream.max_buffers = NUM_PREVIEW_BUFFERS;
    p_stream.priv        = 0;

    streams.push_back(&p_stream);
    streamConfig.num_streams ++;

    if(en_snapshot) {
        s_stream.stream_type = CAMERA3_STREAM_OUTPUT;
        s_stream.width       = s_width;
        s_stream.height      = s_height;
        s_stream.format      = s_halFmt;
        s_stream.data_space  = SNAPSHOT_DS;
        s_stream.usage       = GRALLOC_USAGE_SW_READ_OFTEN;
        s_stream.rotation    = ROTATION_MODE;
        s_stream.max_buffers = NUM_SNAPSHOT_BUFFERS;
        s_stream.priv        = 0;

        streams.push_back(&s_stream);
        streamConfig.num_streams ++;
    }

    streamConfig.streams        = streams.data();

    // Call into the camera module to check for support of the required stream config i.e. the required usecase
    if (pDevice->ops->configure_streams(pDevice, &streamConfig))
    {
        VOXL_LOG_FATAL("voxl-camera-server FATAL: Configure streams failed for camera: %d\n", cameraId);
        return -EINVAL;
    }

    return S_OK;
}


// -----------------------------------------------------------------------------------------------------------------------------
// Construct default camera settings that will be passed to the camera module to be used for capturing the frames
// -----------------------------------------------------------------------------------------------------------------------------
int PerCameraMgr::ConstructDefaultRequestSettings()
{

    // Get the default baseline settings
    camera_metadata_t* pDefaultMetadata =
            (camera_metadata_t *)pDevice->ops->construct_default_request_settings(pDevice, CAMERA3_TEMPLATE_PREVIEW);

    if(en_snapshot){
        pDefaultMetadata =
                    (camera_metadata_t *)pDevice->ops->construct_default_request_settings(pDevice, CAMERA3_TEMPLATE_STILL_CAPTURE);
    }

    // Modify all the settings that we want to
    requestMetadata = clone_camera_metadata(pDefaultMetadata);

    if (usingAE) {

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

    } else {

        uint8_t aeMode            =  ANDROID_CONTROL_AE_MODE_ON;
        uint8_t antibanding       =  ANDROID_CONTROL_AE_ANTIBANDING_MODE_AUTO;
        uint8_t awbMode           =  ANDROID_CONTROL_AWB_MODE_AUTO;

        //Don't have any autofocus so turn these off
        uint8_t afMode            =  ANDROID_CONTROL_AF_MODE_OFF;
        uint8_t faceDetectMode    =  ANDROID_STATISTICS_FACE_DETECT_MODE_OFF;

        requestMetadata.update(ANDROID_CONTROL_AE_MODE,             &aeMode,             1);
        requestMetadata.update(ANDROID_CONTROL_AE_ANTIBANDING_MODE, &antibanding,        1);
        requestMetadata.update(ANDROID_CONTROL_AWB_MODE,            &awbMode,            1);
        requestMetadata.update(ANDROID_STATISTICS_FACE_DETECT_MODE, &faceDetectMode,     1);
        requestMetadata.update(ANDROID_CONTROL_AF_MODE,             &afMode,             1);
    }

    if(en_snapshot){
        uint8_t jpegQuality     = JPEG_DEFUALT_QUALITY;
        //uint8_t ZslEnable       = ANDROID_CONTROL_ENABLE_ZSL_TRUE;

        requestMetadata.update(ANDROID_JPEG_QUALITY, &(jpegQuality), sizeof(jpegQuality));
        //requestMetadata.update(ANDROID_CONTROL_ENABLE_ZSL, &(ZslEnable), 1);
    }

    int fpsRange[] = {configInfo.fps, configInfo.fps};
    int64_t frameDuration = 1e9 / configInfo.fps;

    requestMetadata.update(ANDROID_CONTROL_AE_TARGET_FPS_RANGE, &fpsRange[0],        2);
    requestMetadata.update(ANDROID_SENSOR_FRAME_DURATION,       &frameDuration,      1);

    return 0;

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

    bufferDeleteBuffers(p_bufferGroup);
    bufferDeleteBuffers(v_bufferGroup);
    bufferDeleteBuffers(s_bufferGroup);

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

    for (uint i = 0; i < pHalResult->num_output_buffers; i++)
    {

        VOXL_LOG_VERBOSE("Received output buffer %d from camera %s\n", pHalResult->frame_number, name);

        currentFrameNumber = pHalResult->frame_number;

        // Mutex is required for msgQueue access from here and from within the thread wherein it will be de-queued
        pthread_mutex_lock(&resultMutex);

        // Queue up work for the result thread "ThreadPostProcessResult"
        resultMsgQueue.push_back(pHalResult->output_buffers[i]);
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

static void WriteSnapshot(BufferBlock* bufferBlockInfo, int format, const char* path)
{
    uint64_t size    = bufferBlockInfo->size;
    //uint32_t width   = bufferBlockInfo->width;
    //uint32_t height  = bufferBlockInfo->height;
    //uint32_t stride  = bufferBlockInfo->stride;
    //uint32_t slice   = bufferBlockInfo->slice;

    uint8_t* src_data = (uint8_t*)bufferBlockInfo->vaddress;

    FILE* file_descriptor = fopen(path, "wb");

    if (format == HAL_PIXEL_FORMAT_BLOB) {
        /*
        struct Camera3JPEGBlob cameraJpegBlob;
        size_t jpegOffsetToEof = (size_t)size - (size_t)sizeof(cameraJpegBlob);
        unsigned char* jpegEndOfFile = &src_data[jpegOffsetToEof];
        memcpy(&cameraJpegBlob, jpegEndOfFile, sizeof(Camera3JPEGBlob));

        if (cameraJpegBlob.JPEGBlobId == JPEG_BLOB_ID) {
            fwrite(src_data, cameraJpegBlob.JPEGBlobSize, 1, file_descriptor);
        } else {*/
            fwrite(src_data, size, 1, file_descriptor);
        //}

    }/* else if (format == HAL_PIXEL_FORMAT_YCbCr_420_888) {
        int plane_number  = 2;
        int byte_per_pixel = 1;

        for (int i = 1; i <= plane_number; i++) {
            for (unsigned int h = 0; h < height / i; h++) {
                fwrite(src_data, (width * byte_per_pixel), 1, file_descriptor);
                src_data += stride;
            }
            src_data += stride * (slice - height);
        }

    }*/ else {
        VOXL_LOG_ERROR("%s recieved frame in unsuppored format\n", __FUNCTION__);
    }

    fclose(file_descriptor);
}


void PerCameraMgr::ProcessPreviewFrame(BufferBlock* bufferBlockInfo){

    camera_image_metadata_t imageInfo;
    imageInfo.magic_number = CAMERA_MAGIC_NUMBER;

    //imageInfo.exposure_ns = currentExposure;
    //imageInfo.gain        = currentGain;

    //Temporary solution to prevent oscillating until we figure out how to set this to the registers manually
    imageInfo.exposure_ns = setExposure;
    imageInfo.gain        = setGain;

    uint8_t* srcPixel      = (uint8_t*)bufferBlockInfo->vaddress;
    imageInfo.width        = p_width;
    imageInfo.height       = p_height;
    imageInfo.timestamp_ns = currentTimestamp;
    imageInfo.frame_id     = currentFrameNumber;

    if (p_halFmt == HAL_PIXEL_FORMAT_RAW10)
    {

        // check the first frame to see if we actually got a raw10 frame or if it's actually raw8
        if(imageInfo.frame_id == 1){

            //Only need to set this info once, put in the condition to save a few cycles
            imageInfo.format     = IMAGE_FORMAT_RAW8;
            imageInfo.size_bytes = p_width * p_height;
            imageInfo.stride     = p_width;

            VOXL_LOG_VERBOSE("Received raw10 frame, checking to see if is actually raw8\n");

            if((is10bit = Check10bit(srcPixel, p_width, p_height))){
                VOXL_LOG_VERBOSE("Frame was actually 10 bit, proceeding with conversions\n");
            } else {
                VOXL_LOG_VERBOSE("Frame was actually 8 bit, sending as is\n");
            }

        }

        if(is10bit){
            ConvertTo8bitRaw(srcPixel,
                             p_width,
                             p_height);
        }
    }
    else if (p_halFmt == HAL_PIXEL_FORMAT_YCbCr_420_888)
    {
        // For ov7251 camera there is no color so we just send the Y channel data as RAW8
        if (configInfo.type == CAMTYPE_OV7251)
        {
            imageInfo.format     = IMAGE_FORMAT_RAW8;
            imageInfo.size_bytes = p_width * p_height;
        }
        #ifdef APQ8096
        //APQ only, stereo frames can come in as a pair, need to deinterlace them
        else if (configInfo.type == CAMTYPE_OV7251_PAIR)
        {
            static uint8_t *stereoBuffer = (uint8_t*)malloc(p_width/2 * p_height);

            imageInfo.format     = IMAGE_FORMAT_STEREO_RAW8;
            imageInfo.size_bytes = p_width * p_height;
            imageInfo.width      = p_width / 2;

            for(int i = 0; i < p_height; i++){
                memcpy(&(srcPixel[i * p_width / 2]), &(srcPixel[i * p_width]), p_width / 2);
                memcpy(&(stereoBuffer[i * p_width / 2]), &(srcPixel[(i * p_width) + (p_width/2)]), p_width / 2);
            }
            memcpy(&(srcPixel[p_width/2*p_height]), stereoBuffer, p_width/2*p_height);

        }
        #endif
        // We always send YUV contiguous data out of the camera server
        else {
            imageInfo.format     = IMAGE_FORMAT_NV12;
            bufferMakeYUVContiguous(bufferBlockInfo);
            ///<@todo assuming 420 format and multiplying by 1.5 because NV21/NV12 is 12 bits per pixel
            imageInfo.size_bytes = (bufferBlockInfo->width * bufferBlockInfo->height * 1.5);
        }

        if(configInfo.flip){
            if(imageInfo.frame_id == 0){
                VOXL_LOG_ERROR("Flipping not currently supported for YUV images, writing as-is\n");
            }
        }
    } else {
        VOXL_LOG_ERROR("Camera: %s received invalid preview format, stopping\n", name);
        EStopCameraServer();
    }

    if(partnerMode == MODE_MONO){
        // Ship the frame out of the camera server
        pipe_server_write_camera_frame(outputChannel, imageInfo, srcPixel);
        VOXL_LOG_VERBOSE("Sent frame %d through pipe %s\n", imageInfo.frame_id, name);

        int64_t    new_exposure_ns;
        int32_t    new_gain;

        if (usingAE && expInterface.update_exposure(
                srcPixel,
                p_width,
                p_height,
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
                imageInfo.size_bytes = p_width * p_height * 1.5 * 2;
                break;
            case IMAGE_FORMAT_RAW8:
                imageInfo.format = IMAGE_FORMAT_STEREO_RAW8;
                imageInfo.size_bytes = p_width * p_height * 2;
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
            return;
        }

        if(childFrame == NULL){
            pthread_mutex_unlock(&stereoMutex);
            VOXL_LOG_INFO("Child frame not received\n");
            return;
        }

        //Much newer child, discard master but keep the child
        if(childInfo->timestamp_ns - imageInfo.timestamp_ns > MAX_STEREO_DISCREPENCY_NS){
            VOXL_LOG_INFO("INFO: Camera %s recieved much newer child than master (%lu), discarding master and trying again\n", name, childInfo->timestamp_ns - imageInfo.timestamp_ns);
            pthread_mutex_unlock(&stereoMutex);
            return;
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

        if(configInfo.flip){
            reverse(srcPixel, imageInfo.size_bytes/2);
            reverse(childFrame, imageInfo.size_bytes/2);
        }

        // Ship the frame out of the camera server
        pipe_server_write_stereo_frame(outputChannel, imageInfo, srcPixel, childFrame);
        VOXL_LOG_VERBOSE("Sent frame %d through pipe %s\n", imageInfo.frame_id, name);

        // Run Auto Exposure
        int64_t    new_exposure_ns;
        int32_t    new_gain;

        if (usingAE && expInterface.update_exposure(
                srcPixel,
                p_width,
                p_height,
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

        otherMgr->childFrame = srcPixel;
        otherMgr->childInfo  = &imageInfo;

        pthread_cond_wait(&stereoCond, &(otherMgr->stereoMutex));
        pthread_mutex_unlock(&(otherMgr->stereoMutex));
    }

}
void PerCameraMgr::ProcessSnapshotFrame(BufferBlock* bufferBlockInfo){

    static int counter = 1;

    char buffer[128];

    sprintf(buffer, "/data/screenshots/%s-%d.jpg", name, counter);

    VOXL_LOG_VERBOSE("--Writing snapshot to :\"%s\"\n", buffer);
    WriteSnapshot(bufferBlockInfo, s_halFmt, buffer);

    counter++;

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
    { // Configuration, these variables don't need to persist
        char buf[16];
        pid_t tid = syscall(SYS_gettid);
        sprintf(buf, "cam%d-result", cameraId);
        pthread_setname_np(pthread_self(), buf);
        VOXL_LOG_VERBOSE("Entered thread: %s(tid: %lu)\n", buf, tid);

        // Set thread priority
        int which = PRIO_PROCESS;
        int nice  = -10;
        setpriority(which, tid, nice);
    }

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

        buffer_handle_t  *handle      = resultMsgQueue.front().buffer;
        camera3_stream_t *stream      = resultMsgQueue.front().stream;
        BufferGroup      *bufferGroup = GetBufferGroup(stream);

        // Coming here means we have a result frame to process
        VOXL_LOG_VERBOSE("%s procesing new buffer\n", name);

        resultMsgQueue.pop_front();
        pthread_mutex_unlock(&resultMutex);

        if(stopped) {
            bufferPush(*bufferGroup, handle);
            pthread_cond_signal(&stereoCond);
            continue;
        }

        BufferBlock* pBufferInfo  = bufferGetBufferInfo(bufferGroup, handle);
        switch (GetStreamId(stream)){
            case STREAM_PREVIEW:
                VOXL_LOG_VERBOSE("Camera: %s processing preview frame\n", name);
                ProcessPreviewFrame(pBufferInfo);
                break;

            case STREAM_VIDEO: // Not Ready
                VOXL_LOG_VERBOSE("Camera: %s processing video frame\n", name);
                //ProcessVideoFrame(pBufferInfo);
                break;

            case STREAM_SNAPSHOT:
                VOXL_LOG_VERBOSE("Camera: %s processing snapshot frame\n", name);
                ProcessSnapshotFrame(pBufferInfo);
                break;

            default:
                VOXL_LOG_ERROR("Camera: %s recieved frame for unknown stream\n", name);
                break;
        }

        bufferPush(*bufferGroup, handle); // This queues up the buffer for recycling

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

    if(usingAE){
        requestMetadata.update(ANDROID_SENSOR_EXPOSURE_TIME, &setExposure, 1);
        requestMetadata.update(ANDROID_SENSOR_SENSITIVITY,   &setGain, 1);
    }

    std::vector<camera3_stream_buffer_t> streamBufferList;
    request.num_output_buffers  = 0;

    camera3_stream_buffer_t pstreamBuffer;
    pstreamBuffer.buffer        = (const native_handle_t**)bufferPop(p_bufferGroup);
    pstreamBuffer.stream        = &p_stream;
    pstreamBuffer.status        = 0;
    pstreamBuffer.acquire_fence = -1;
    pstreamBuffer.release_fence = -1;

    request.num_output_buffers ++;
    streamBufferList.push_back(pstreamBuffer);

    if(en_snapshot){
        camera3_stream_buffer_t sstreamBuffer;
        sstreamBuffer.buffer        = (const native_handle_t**)bufferPop(s_bufferGroup);
        sstreamBuffer.stream        = &s_stream;
        sstreamBuffer.status        = 0;
        sstreamBuffer.acquire_fence = -1;
        sstreamBuffer.release_fence = -1;

        request.num_output_buffers ++;
        streamBufferList.push_back(sstreamBuffer);
    }

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
    sprintf(buf, "cam%d-request", cameraId);
    pthread_setname_np(pthread_self(), buf);

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

enum AECommandVals {
    SET_EXP_GAIN,
    SET_EXP,
    SET_GAIN,
    START_AE,
    STOP_AE,
    SNAPSHOT
};
static const char* CmdStrings[] = {
    "set_exp_gain",
    "set_exp",
    "set_gain",
    "start_ae",
    "stop_ae",
    "snapshot"
};

int PerCameraMgr::SetupPipes(){

    //Set up the connect callback (wrapped in a lambda because it's a member function)
    pipe_server_set_connect_cb(
            outputChannel,                                         //Channel
            [](int ch, int client_id, char* name, void* context)   //Callback
                    {((PerCameraMgr*)context)->addClient();},
            this);
    //Set up the control callback (wrapped in a lambda because it's a member function)
    pipe_server_set_control_cb(
            outputChannel,                                         //Channel
            [](int ch, char * string, int bytes, void* context)    //Callback
                    {((PerCameraMgr*)context)->HandleControlCmd(string);},
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

void PerCameraMgr::HandleControlCmd(char* cmd) {

    __attribute__((unused)) const int MIN_EXP  = configInfo.expGainInfo.exposure_min_us;
    __attribute__((unused)) const int MAX_EXP  = configInfo.expGainInfo.exposure_max_us;
    __attribute__((unused)) const int MIN_GAIN = configInfo.expGainInfo.gain_min;
    __attribute__((unused)) const int MAX_GAIN = configInfo.expGainInfo.gain_max;

    /**************************
     *
     * SET Exposure and Gain
     *
     */
    // if(strncmp(cmd, CmdStrings[SET_EXP_GAIN], strlen(CmdStrings[SET_EXP_GAIN])) == 0){

    //     char buffer[strlen(CmdStrings[SET_EXP_GAIN])+1];
    //     float exp = -1.0;
    //     int gain = -1;

    //     if(sscanf(cmd, "%s %f %d", buffer, &exp, &gain) == 3){
    //         bool valid = true;
    //         if(exp < MIN_EXP || exp > MAX_EXP){
    //             valid = false;
    //             VOXL_LOG_ERROR("Invalid Control Pipe Exposure: %f,\n\tShould be between %f and %f\n", exp, MIN_EXP, MAX_EXP);
    //         }
    //         if(gain < MIN_GAIN || gain > MAX_GAIN){
    //             valid = false;
    //             VOXL_LOG_ERROR("Invalid Control Pipe Gain: %d,\n\tShould be between %d and %d\n", gain, MIN_GAIN, MAX_GAIN);
    //         }
    //         if(valid){
    //             pCameraMgr->SetUsingAE(false);
    //             VOXL_LOG_INFO("Camera: %s recieved new exp/gain values: %6.3f(ms) %d\n", pCameraMgr->GetName(), exp, gain);
    //             pCameraMgr->SetNextExpGain(exp*1000000, gain);
    //         }
    //     } else {
    //         VOXL_LOG_ERROR("Camera: %s failed to get valid exposure/gain values from control pipe\n", pCameraMgr->GetName());
    //         VOXL_LOG_ERROR("\tShould follow format: \"%s 25 350\"\n", CmdStrings[SET_EXP_GAIN]);
    //     }

    // }
    // /**************************
    //  *
    //  * SET Exposure
    //  *
    //  */ else if(strncmp(cmd, CmdStrings[SET_EXP], strlen(CmdStrings[SET_EXP])) == 0){

    //     char buffer[strlen(CmdStrings[SET_EXP])+1];
    //     float exp = -1.0;

    //     if(sscanf(cmd, "%s %f", buffer, &exp) == 2){
    //         bool valid = true;
    //         if(exp < MIN_EXP || exp > MAX_EXP){
    //             valid = false;
    //             VOXL_LOG_ERROR("Invalid Control Pipe Exposure: %f,\n\tShould be between %f and %f\n", exp, MIN_EXP, MAX_EXP);
    //         }
    //         if(valid){
    //             pCameraMgr->SetUsingAE(false);
    //             VOXL_LOG_INFO("Camera: %s recieved new exp value: %6.3f(ms)\n", pCameraMgr->GetName(), exp);
    //             pCameraMgr->SetNextExpGain(exp*1000000, pCameraMgr->GetCurrentGain());
    //         }
    //     } else {
    //         VOXL_LOG_ERROR("Camera: %s failed to get valid exposure value from control pipe\n", pCameraMgr->GetName());
    //         VOXL_LOG_ERROR("\tShould follow format: \"%s 25\"\n", CmdStrings[SET_EXP]);
    //     }
    // }
    // /**************************
    //  *
    //  * SET Gain
    //  *
    //  */ else if(strncmp(cmd, CmdStrings[SET_GAIN], strlen(CmdStrings[SET_GAIN])) == 0){

    //     char buffer[strlen(CmdStrings[SET_GAIN])+1];
    //     int gain = -1;

    //     if(sscanf(cmd, "%s %d", buffer, &gain) == 2){
    //         bool valid = true;
    //         if(gain < MIN_GAIN || gain > MAX_GAIN){
    //             valid = false;
    //             VOXL_LOG_ERROR("Invalid Control Pipe Gain: %d,\n\tShould be between %d and %d\n", gain, MIN_GAIN, MAX_GAIN);
    //         }
    //         if(valid){
    //             pCameraMgr->SetUsingAE(false);
    //             VOXL_LOG_INFO("Camera: %s recieved new gain value: %d\n", pCameraMgr->GetName(), gain);
    //             pCameraMgr->SetNextExpGain(pCameraMgr->GetCurrentExposure(), gain);
    //         }
    //     } else {
    //         VOXL_LOG_ERROR("Camera: %s failed to get valid gain value from control pipe\n", pCameraMgr->GetName());
    //         VOXL_LOG_ERROR("\tShould follow format: \"%s 350\"\n", CmdStrings[SET_GAIN]);
    //     }
    // }

    // /**************************
    //  *
    //  * START Auto Exposure
    //  *
    //  */ else if(strncmp(cmd, CmdStrings[START_AE], strlen(CmdStrings[START_AE])) == 0){
    //     pCameraMgr->SetUsingAE(true);
    //     //Use this to awaken the process capture result block and avoid race conditions
    //     pCameraMgr->SetNextExpGain(pCameraMgr->GetCurrentExposure(), pCameraMgr->GetCurrentGain());
    //     VOXL_LOG_INFO("Camera: %s starting to use Auto Exposure\n", pCameraMgr->GetName());
    // }
    // /**************************
    //  *
    //  * STOP Auto Exposure
    //  *
    //  */ else if(strncmp(cmd, CmdStrings[STOP_AE], strlen(CmdStrings[STOP_AE])) == 0){
    //     if(pCameraMgr->IsUsingAE()){
    //         VOXL_LOG_INFO("Camera: %s ceasing to use Auto Exposure\n", pCameraMgr->GetName());
    //         pCameraMgr->SetUsingAE(false);
    //     }
    // } else

    /**************************
     *
     * START Auto Exposure
     *
     */
    if(strncmp(cmd, CmdStrings[SNAPSHOT], strlen(CmdStrings[SNAPSHOT])) == 0){
        if(en_snapshot){
            VOXL_LOG_INFO("Camera: %s taking snapshot\n", name);

        } else {
            VOXL_LOG_ERROR("Camera: %s failed to take snapshot, mode not enabled\n", name);
        }
    } else

    /**************************
     *
     * Unknown Command
     *
     */
    {
        VOXL_LOG_ERROR("Camera: %s got unknown Command: %s\n", name, string);
    }

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

static int estimateJpegBufferSize(camera_metadata_t* cameraCharacteristics, uint32_t width, uint32_t height)
{

    int maxJpegBufferSize = 0;
    camera_metadata_ro_entry jpegBufferMaxSize;
    find_camera_metadata_ro_entry(cameraCharacteristics,
                                        ANDROID_JPEG_MAX_SIZE,
                                        &jpegBufferMaxSize);
    if (jpegBufferMaxSize.count == 0) {
        fprintf(stderr, "Find maximum JPEG size from metadat failed.!\n");
        return 0;
    }
    maxJpegBufferSize = jpegBufferMaxSize.data.i32[0];

    float scaleFactor = ((float)width * (float)height) /
        (((float)maxJpegBufferSize - (float)sizeof(camera3_jpeg_blob)) / 3.0f);
    int jpegBufferSize = minJpegBufferSize + (maxJpegBufferSize - minJpegBufferSize) * scaleFactor;

    return jpegBufferSize;
}

static int32_t HalFmtFromType(int fmt){
    //Always request raw10 frames to make sure we have a buffer for either,
    // post processing thread will figure out what the driver is giving us
    if (fmt == FMT_RAW10 ||
        fmt == FMT_RAW8)
    {
        return HAL_PIXEL_FORMAT_RAW10;
    }
    else if ((fmt == FMT_NV21) ||
             (fmt == FMT_NV12))
    {
        return HAL_PIXEL_FORMAT_YCbCr_420_888;
    } else {
        VOXL_LOG_ERROR("ERROR: Invalid Preview Format!\n");

        throw -EINVAL;
    }
}
