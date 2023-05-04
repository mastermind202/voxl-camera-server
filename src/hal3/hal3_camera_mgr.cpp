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
#include <string.h>
#include <camera/CameraMetadata.h>
#include <hardware/camera_common.h>
#include <algorithm>
#include <voxl_cutils.h>
#include <royale/DepthData.hpp>
#include <tof_interface.hpp>
#include <modal_journal.h>
#include <modal_pipe.h>

#include <cpu_monitor_interface.h>

#include "buffer_manager.h"
#include "common_defs.h"
#include "hal3_camera.h"
#include "voxl_camera_server.h"
#include "voxl_cutils.h"

#define CAM_CONTROL_COMMANDS "set_exp_gain,set_exp,set_gain,start_ae,stop_ae"

#define NUM_PREVIEW_BUFFERS  32
#define NUM_SNAPSHOT_BUFFERS 8
#define STREAM_ALLOWED_ITEMS_IN_OMX_QUEUE 1 // favor latency when dropping frames
#define RECORD_ALLOWED_ITEMS_IN_OMX_QUEUE 2 // only drop frames when really getting behind

#define JPEG_DEFUALT_QUALITY        75

#define abs(x,y) ((x) > (y) ? (x) : (y))

#define MAX_STEREO_DISCREPENCY_NS ((1000000000/configInfo.fps)*0.9)
#define CPU_CH	3

// Platform Specific Flags
#ifdef APQ8096
    #define ROTATION_MODE  CAMERA3_STREAM_ROTATION_0
    #define OPERATION_MODE QCAMERA3_VENDOR_STREAM_CONFIGURATION_RAW_ONLY_MODE
    #define ENCODER_USAGE  GRALLOC_USAGE_HW_COMPOSER | GRALLOC_USAGE_HW_TEXTURE
    #define SNAPSHOT_DS    HAL_DATASPACE_JFIF
    #define NUM_STREAM_BUFFERS   16
    #define NUM_RECORD_BUFFERS   16
#elif QRB5165
    #define ROTATION_MODE  2
    #define OPERATION_MODE CAMERA3_STREAM_CONFIGURATION_NORMAL_MODE
    #define ENCODER_USAGE  GRALLOC_USAGE_HW_VIDEO_ENCODER
    #define SNAPSHOT_DS    HAL_DATASPACE_V0_JFIF
    #define NUM_STREAM_BUFFERS   10 // shouldn't need more than 10, if the buffer pool is empty then OMX should be dropping more frames
    #define NUM_RECORD_BUFFERS   10 // shouldn't need more than 10, if the buffer pool is empty then OMX should be dropping more frames
#else
    #error "No Platform defined"
#endif

static const int  minJpegBufferSize = sizeof(camera3_jpeg_blob) + 1024 * 512;

static int estimateJpegBufferSize(camera_metadata_t* cameraCharacteristics, uint32_t width, uint32_t height);

// Code base to monitor CPU
static int standby_active = 0;

static void _cpu_connect_cb(__attribute__((unused)) int ch, __attribute__((unused)) void* context)
{
    M_DEBUG("Connected to cpu-monitor\n");
    return;
}

static void _cpu_disconnect_cb(__attribute__((unused)) int ch, __attribute__((unused)) void* context)
{
    M_DEBUG("Disconnected from cpu-monitor\n");
    return;
}


// called whenever the simple helper has data for us to process
static void _cpu_helper_cb(__attribute__((unused))int ch, char* raw_data, int bytes, __attribute__((unused)) void* context)
{
    int n_packets;
    cpu_stats_t *data_array = modal_cpu_validate_pipe_data(raw_data, bytes, &n_packets);
    if (data_array == NULL){
        M_DEBUG("Data array is null");
        return;
    }
    // only use most recent packet
    cpu_stats_t data = data_array[n_packets-1];
    
    if(data.flags&CPU_STATS_FLAG_STANDBY_ACTIVE){
        if(!standby_active){
            M_DEBUG("Entering standby mode\n");
            standby_active = 1;
        }
    }
    else{
        if(standby_active){
            M_DEBUG("Exiting standby mode\n");
            standby_active = 0;
        }
    }
    M_DEBUG("Value of standby_active is: %i \n", standby_active);
    return;
}

// -----------------------------------------------------------------------------------------------------------------------------
// Constructor
// -----------------------------------------------------------------------------------------------------------------------------
PerCameraMgr::PerCameraMgr(PerCameraInfo pCameraInfo) :
    configInfo        (pCameraInfo),
    outputChannel     (pipe_server_get_next_available_channel()),
    cameraId          (pCameraInfo.camId),
    en_stream         (pCameraInfo.en_stream),
    en_record         (pCameraInfo.en_record),
    en_snapshot       (pCameraInfo.en_snapshot),
    fps               (pCameraInfo.fps),
    pre_width         (pCameraInfo.pre_width),
    pre_height        (pCameraInfo.pre_height),
    pre_halfmt        (HalFmtFromType(pCameraInfo.pre_format)),
    str_width         (pCameraInfo.str_width),
    str_height        (pCameraInfo.str_height),
    str_halfmt        (HAL_PIXEL_FORMAT_YCbCr_420_888),
    str_bitrate       (pCameraInfo.str_bitrate),
    rec_width         (pCameraInfo.rec_width),
    rec_height        (pCameraInfo.rec_height),
    rec_halfmt        (HAL_PIXEL_FORMAT_YCbCr_420_888),
    rec_bitrate       (pCameraInfo.rec_bitrate),
    snap_width        (pCameraInfo.snap_width),
    snap_height       (pCameraInfo.snap_height),
    snap_halfmt       (HAL_PIXEL_FORMAT_BLOB),
    ae_mode           (pCameraInfo.ae_mode),
    pCameraModule     (HAL3_get_camera_module()),
    pVideoEncoderStream(NULL),
    pVideoEncoderRecord(NULL),
    expHistInterface  (pCameraInfo.ae_hist_info),
    expMSVInterface   (pCameraInfo.ae_msv_info)
{

    strcpy(name, pCameraInfo.name);

    cameraCallbacks.cameraCallbacks = {&CameraModuleCaptureResult, &CameraModuleNotify};
    cameraCallbacks.pPrivate        = this;

    if(pCameraModule == NULL ){
        M_ERROR("Failed to get HAL module!\n");

        throw -EINVAL;
    }

    // Check if the stream configuration is supported by the camera or not. If cameraid doesnt support the stream configuration
    // we just exit. The stream configuration is checked into the static metadata associated with every camera.
    if (!HAL3_is_config_supported(cameraId, pre_width, pre_height, pre_halfmt))
    {
        M_ERROR("Camera %d failed to find supported preview config: %dx%d\n", cameraId, pre_width, pre_height);

        throw -EINVAL;
    }
    if (en_stream && !HAL3_is_config_supported(cameraId, str_width, str_height, str_halfmt))
    {
        M_ERROR("Camera %d failed to find supported stream config: %dx%d\n", cameraId, str_width, str_height);

        throw -EINVAL;
    }
    if (en_record && !HAL3_is_config_supported(cameraId, rec_width, rec_height, rec_halfmt))
    {
        M_ERROR("Camera %d failed to find supported record config: %dx%d\n", cameraId, rec_width, rec_height);

        throw -EINVAL;
    }
    if (en_snapshot && !HAL3_is_config_supported(cameraId, snap_width, snap_height, snap_halfmt))
    {
        M_ERROR("Camera %d failed to find supported snapshot config: %dx%d\n", cameraId, snap_width, snap_height);

        throw -EINVAL;
    }

    char cameraName[20];
    sprintf(cameraName, "%d", cameraId);

    if (pCameraModule->common.methods->open(&pCameraModule->common, cameraName, (hw_device_t**)(&pDevice)))
    {
        M_ERROR("Open camera %s failed!\n", name);

        throw -EINVAL;
    }

    if (pDevice->ops->initialize(pDevice, (camera3_callback_ops*)&cameraCallbacks))
    {
        M_ERROR("Initialize camera %s failed!\n", name);

        throw -EINVAL;
    }

    if (ConfigureStreams())
    {
        M_ERROR("Failed to configure streams for camera: %s\n", name);

        throw -EINVAL;
    }

    if (bufferAllocateBuffers(pre_bufferGroup,
                              NUM_PREVIEW_BUFFERS,
                              pre_stream.width,
                              pre_stream.height,
                              pre_stream.format,
                              pre_stream.usage)) {
        M_ERROR("Failed to allocate preview buffers for camera: %s\n", name);

        throw -EINVAL;
    }
    M_DEBUG("Successfully set up pipeline for stream: PREVIEW\n\n");

    if (en_stream) {

        if (bufferAllocateBuffers(str_bufferGroup,
                                  NUM_STREAM_BUFFERS,
                                  str_stream.width,
                                  str_stream.height,
                                  str_stream.format,
                                  str_stream.usage)) {
            M_ERROR("Failed to allocate encode buffers for camera: %s\n", name);
            throw -EINVAL;
        }

        try{
            streamOutputChannel = pipe_server_get_next_available_channel();
            VideoEncoderConfig enc_info = {
                .width =             (uint32_t)str_width,   ///< Image width
                .height =            (uint32_t)str_height,  ///< Image height
                .format =            (uint32_t)str_halfmt,  ///< Image format
                .isBitRateConstant = true,                  ///< Is the bit rate constant
                .targetBitRate =     str_bitrate,           ///< Desired target bitrate
                .frameRate =         pCameraInfo.fps,       ///< Frame rate
                .isH265 =            false,                 ///< Is it H265 encoding or H264
                .inputBuffers =      &str_bufferGroup,
                .outputPipe =        streamOutputChannel
            };
            pVideoEncoderStream = new VideoEncoder(&enc_info);
        } catch(int) {
            M_ERROR("Failed to initialize encoder for camera: %s\n", name);
            throw -EINVAL;
        }
        M_DEBUG("Successfully set up pipeline for stream: STREAM\n\n");
    }

    if (en_record) {

        if (bufferAllocateBuffers(rec_bufferGroup,
                                  NUM_RECORD_BUFFERS,
                                  rec_stream.width,
                                  rec_stream.height,
                                  rec_stream.format,
                                  rec_stream.usage)) {
            M_ERROR("Failed to allocate encode buffers for camera: %s\n", name);
            throw -EINVAL;
        }

        try{
            recordOutputChannel = pipe_server_get_next_available_channel();
            VideoEncoderConfig enc_info = {
                .width =             (uint32_t)rec_width,   ///< Image width
                .height =            (uint32_t)rec_height,  ///< Image height
                .format =            (uint32_t)rec_halfmt,  ///< Image format
                .isBitRateConstant = true,                  ///< Is the bit rate constant
                .targetBitRate =     rec_bitrate,           ///< Desired target bitrate
                .frameRate =         pCameraInfo.fps,       ///< Frame rate
                .isH265 =            false,                 ///< Is it H265 encoding or H264
                .inputBuffers =      &rec_bufferGroup,
                .outputPipe =        recordOutputChannel
            };
            pVideoEncoderRecord = new VideoEncoder(&enc_info);
        } catch(int) {
            M_ERROR("Failed to initialize encoder for camera: %s\n", name);
            throw -EINVAL;
        }
        M_DEBUG("Successfully set up pipeline for stream: RECORD\n\n");
    }

    if (en_snapshot) {

        camera_info halCameraInfo;
        pCameraModule->get_camera_info(cameraId, &halCameraInfo);
        camera_metadata_t* pStaticMetadata = (camera_metadata_t *)halCameraInfo.static_camera_characteristics;

        int blobWidth = estimateJpegBufferSize(pStaticMetadata, snap_width, snap_height);

        if(bufferAllocateBuffers(snap_bufferGroup,
                                 NUM_SNAPSHOT_BUFFERS,
                                 blobWidth,
                                 1,
                                 snap_stream.format,
                                 snap_stream.usage)) {
            M_ERROR("Failed to allocate snapshot buffers for camera: %s\n", name);

            throw -EINVAL;
        }
        M_DEBUG("Successfully set up pipeline for stream: SNAPSHOT\n\n");
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
        newInfo.en_stream = false;
        newInfo.en_record = false;
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

    streamConfig.num_streams    = 0;

    pre_stream.stream_type = CAMERA3_STREAM_OUTPUT;
    pre_stream.width       = pre_width;
    pre_stream.height      = pre_height;
    pre_stream.format      = pre_halfmt;
    pre_stream.data_space  = HAL_DATASPACE_UNKNOWN;
    pre_stream.usage       = GRALLOC_USAGE_HW_COMPOSER | GRALLOC_USAGE_HW_TEXTURE;
    pre_stream.rotation    = ROTATION_MODE;
    pre_stream.max_buffers = NUM_PREVIEW_BUFFERS;
    pre_stream.priv        = 0;

    streams.push_back(&pre_stream);
    streamConfig.num_streams ++;

    if(en_stream) {
        str_stream.stream_type = CAMERA3_STREAM_OUTPUT;
        str_stream.width       = str_width;
        str_stream.height      = str_height;
        str_stream.format      = str_halfmt;
        str_stream.data_space  = HAL_DATASPACE_UNKNOWN;
        str_stream.usage       = ENCODER_USAGE;
        str_stream.rotation    = ROTATION_MODE;
        str_stream.max_buffers = NUM_STREAM_BUFFERS;
        str_stream.priv        = 0;

        streams.push_back(&str_stream);
        streamConfig.num_streams ++;
    }

    if(en_record) {
        rec_stream.stream_type = CAMERA3_STREAM_OUTPUT;
        rec_stream.width       = rec_width;
        rec_stream.height      = rec_height;
        rec_stream.format      = rec_halfmt;
        rec_stream.data_space  = HAL_DATASPACE_UNKNOWN;
        rec_stream.usage       = ENCODER_USAGE;
        rec_stream.rotation    = ROTATION_MODE;
        rec_stream.max_buffers = NUM_RECORD_BUFFERS;
        rec_stream.priv        = 0;

        streams.push_back(&rec_stream);
        streamConfig.num_streams ++;
    }

    if(en_snapshot) {
        snap_stream.stream_type = CAMERA3_STREAM_OUTPUT;
        snap_stream.width       = snap_width;
        snap_stream.height      = snap_height;
        snap_stream.format      = snap_halfmt;
        snap_stream.data_space  = SNAPSHOT_DS;
        snap_stream.usage       = GRALLOC_USAGE_SW_READ_OFTEN;
        snap_stream.rotation    = ROTATION_MODE;
        snap_stream.max_buffers = NUM_SNAPSHOT_BUFFERS;
        snap_stream.priv        = 0;

        streams.push_back(&snap_stream);
        streamConfig.num_streams ++;
    }

    num_streams = streamConfig.num_streams;
    streamConfig.streams        = streams.data();
    streamConfig.operation_mode = OPERATION_MODE;
#ifdef QRB5165
    pSessionParams = allocate_camera_metadata(2, 8);
    int32_t frame_rate_rate[] = {fps,fps};
    add_camera_metadata_entry(pSessionParams,
                              ANDROID_CONTROL_AE_TARGET_FPS_RANGE,
                              frame_rate_rate, 2);

    streamConfig.session_parameters = pSessionParams;
#endif
    // Call into the camera module to check for support of the required stream config i.e. the required usecase
    if (pDevice->ops->configure_streams(pDevice, &streamConfig))
    {
        M_ERROR("Configure streams failed for camera: %d\n", cameraId);
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
                    //(camera_metadata_t *)pDevice->ops->construct_default_request_settings(pDevice, CAMERA3_TEMPLATE_STILL_CAPTURE);
                    (camera_metadata_t *)pDevice->ops->construct_default_request_settings(pDevice, CAMERA3_TEMPLATE_VIDEO_RECORD);
    }

    // Modify all the settings that we want to
    requestMetadata = clone_camera_metadata(pDefaultMetadata);

    switch (ae_mode){

        case AE_OFF :
        case AE_LME_HIST :
        case AE_LME_MSV : {

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

            break;
        }

        case AE_ISP : {

            uint8_t aeMode            =  ANDROID_CONTROL_AE_MODE_ON;
            uint8_t antibanding       =  ANDROID_CONTROL_AE_ANTIBANDING_MODE_OFF;
            uint8_t awbMode           =  ANDROID_CONTROL_AWB_MODE_AUTO;

            //Don't have any autofocus so turn these off
            uint8_t afMode            =  ANDROID_CONTROL_AF_MODE_OFF;
            uint8_t faceDetectMode    =  ANDROID_STATISTICS_FACE_DETECT_MODE_OFF;

            requestMetadata.update(ANDROID_CONTROL_AE_MODE,             &aeMode,             1);
            requestMetadata.update(ANDROID_CONTROL_AE_ANTIBANDING_MODE, &antibanding,        1);
            requestMetadata.update(ANDROID_CONTROL_AWB_MODE,            &awbMode,            1);
            requestMetadata.update(ANDROID_STATISTICS_FACE_DETECT_MODE, &faceDetectMode,     1);
            requestMetadata.update(ANDROID_CONTROL_AF_MODE,             &afMode,             1);
            break;
        }
    }

    if(en_snapshot){
        uint8_t jpegQuality     = JPEG_DEFUALT_QUALITY;

        requestMetadata.update(ANDROID_JPEG_QUALITY, &(jpegQuality), sizeof(jpegQuality));
    }

    int fpsRange[] = {configInfo.fps, configInfo.fps};
    int64_t frameDuration = 1e9 / configInfo.fps;

    requestMetadata.update(ANDROID_CONTROL_AE_TARGET_FPS_RANGE, &fpsRange[0],        2);
    requestMetadata.update(ANDROID_SENSOR_FRAME_DURATION,       &frameDuration,      1);

    if(configInfo.type == CAMTYPE_TOF) {
        if(configInfo.standby_enabled){
            pipe_client_set_connect_cb(CPU_CH, _cpu_connect_cb, NULL);
            pipe_client_set_disconnect_cb(CPU_CH, _cpu_disconnect_cb, NULL);
            pipe_client_set_simple_helper_cb(CPU_CH, _cpu_helper_cb, NULL);
            int ret = pipe_client_open(CPU_CH, "cpu_monitor", PROCESS_NAME, \
                CLIENT_FLAG_EN_SIMPLE_HELPER, CPU_STATS_RECOMMENDED_READ_BUF_SIZE);
            // check for error
            if(ret<0){
                M_DEBUG("Failed to open CPU pipe\n");
                pipe_print_error(ret);
            } else {
                M_DEBUG("Starting CPU pipe monitor\n");
            }
        }

        setExposure = 2259763;
        setGain     = 200;

        if(configInfo.fps != 5 && configInfo.fps != 15) {
            M_ERROR("Invalid TOF framerate: %d, must be either 5 or 15\n", configInfo.fps);
            return -1;
        }

        RoyaleListenerType dataType = RoyaleListenerType::LISTENER_DEPTH_DATA;

        TOFInitializationData initializationData = { 0 };

        initializationData.pDataTypes    = &dataType;
        initializationData.numDataTypes  = 1;
        initializationData.pListener     = this;
        initializationData.frameRate     = configInfo.fps;
        initializationData.range         = RoyaleDistanceRange::LONG_RANGE;

        #ifdef APQ8096
            VCU_silent(
                tof_interface = TOFCreateInterface();
            )
            if( tof_interface == NULL) {
                M_ERROR("Failed to create tof interface\n");
                return -1;
            }
            initializationData.pTOFInterface = tof_interface;
            int ret;
            VCU_silent(
                ret = TOFInitialize(&initializationData);
            )
            if(ret) {
                M_ERROR("Failed to initialize tof interface\n");
                return -1;
            }
        #elif QRB5165
            initializationData.cameraId      = cameraId;
            tof_interface = new TOFInterface(&initializationData);
        #endif
        M_VERBOSE("TOF interface created!\n");
    }

    return 0;

}

// -----------------------------------------------------------------------------------------------------------------------------
// This function opens the camera and starts sending the capture requests
// -----------------------------------------------------------------------------------------------------------------------------
void PerCameraMgr::Start()
{

    if(partnerMode != MODE_STEREO_SLAVE){
        if(SetupPipes()){
            M_ERROR("Failed to setup pipes for camera: %s\n", name);

            throw -EINVAL;
        }
    }

    if(pVideoEncoderStream) {
        pVideoEncoderStream->Start();
    }

    if(pVideoEncoderRecord) {
        pVideoEncoderRecord->Start();
    }

    pthread_condattr_t condAttr;
    pthread_condattr_init(&condAttr);
    pthread_condattr_setclock(&condAttr, CLOCK_MONOTONIC);
    pthread_mutex_init(&resultMutex, NULL);
    pthread_mutex_init(&stereoMutex, NULL);
    pthread_mutex_init(&aeMutex, NULL);
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

    pthread_join(requestThread, NULL);

    pthread_cond_broadcast(&stereoCond);
    pthread_cond_broadcast(&resultCond);
    pthread_join(resultThread, NULL);
    pthread_cond_signal(&resultCond);
    pthread_mutex_unlock(&resultMutex);
    pthread_mutex_destroy(&resultMutex);
    pthread_cond_destroy(&resultCond);

    if(partnerMode == MODE_STEREO_MASTER){
        otherMgr->Stop();
    }

    if(pVideoEncoderStream) {
        pVideoEncoderStream->Stop();
        delete pVideoEncoderStream;
    }

    if(pVideoEncoderRecord) {
        pVideoEncoderRecord->Stop();
        delete pVideoEncoderRecord;
    }

    bufferDeleteBuffers(pre_bufferGroup);
    bufferDeleteBuffers(str_bufferGroup);
    bufferDeleteBuffers(rec_bufferGroup);
    bufferDeleteBuffers(snap_bufferGroup);

    if (pDevice != NULL)
    {
        pDevice->common.close(&pDevice->common);
        pDevice = NULL;
    }

    if (pSessionParams != NULL)
    {
        free_camera_metadata(pSessionParams);
    }

    pthread_mutex_destroy(&stereoMutex);
    pthread_cond_destroy(&stereoCond);

    pthread_mutex_destroy(&aeMutex);

    pipe_server_close(outputChannel);

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

        camera_image_metadata_t meta;
        meta.frame_id=pHalResult->frame_number;
        meta.framerate = configInfo.fps;

        M_VERBOSE("Received metadata for frame %d from camera %s\n", pHalResult->frame_number, name);

        int result = 0;
        camera_metadata_ro_entry entry;

        result = find_camera_metadata_ro_entry(pHalResult->result, ANDROID_SENSOR_TIMESTAMP, &entry);

        if (!result && entry.count)
        {
            meta.timestamp_ns = entry.data.i64[0];
            M_VERBOSE("\tTimestamp: %llu\n", meta.timestamp_ns);
        }

        result = find_camera_metadata_ro_entry(pHalResult->result, ANDROID_SENSOR_SENSITIVITY, &entry);

        if (!result && entry.count)
        {
            meta.gain = entry.data.i32[0];
            M_VERBOSE("\tGain: %d\n", meta.gain);
        }

        result = find_camera_metadata_ro_entry(pHalResult->result, ANDROID_SENSOR_EXPOSURE_TIME, &entry);

        if (!result && entry.count)
        {
            meta.exposure_ns = entry.data.i64[0];
            M_VERBOSE("\tExposure: %ld\n", meta.exposure_ns);
        }

        resultMetaRing.insert_data(meta);

    }

    for (uint i = 0; i < pHalResult->num_output_buffers; i++)
    {

        M_VERBOSE("Received output buffer %d from camera %s\n", pHalResult->frame_number, name);

        // Mutex is required for msgQueue access from here and from within the thread wherein it will be de-queued
        pthread_mutex_lock(&resultMutex);

        // Queue up work for the result thread "ThreadPostProcessResult"
        resultMsgQueue.push_back({pHalResult->frame_number, pHalResult->output_buffers[i]});
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
    M_VERBOSE("recieved notify for: %d\n", pHalResult->frame_number);
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
        switch (msg->message.error.error_code) {

            case CAMERA3_MSG_ERROR_DEVICE:
                //Another thread has already detected the fatal error, return since it has already been handled
                if(pPerCameraMgr->EStopped) return;

                M_ERROR("Recieved \"Device\" error from camera: %s\n",
                             pPerCameraMgr->name);
                M_PRINT(  "                          Camera server will be stopped\n");
                EStopCameraServer();
                break;
            case CAMERA3_MSG_ERROR_REQUEST:
                M_ERROR("Recieved \"Request\" error from camera: %s\n",
                             pPerCameraMgr->name);
                break;
            case CAMERA3_MSG_ERROR_RESULT:
                M_ERROR("Recieved \"Result\" error from camera: %s\n",
                             pPerCameraMgr->name);
                break;
            case CAMERA3_MSG_ERROR_BUFFER:
                M_ERROR("Recieved \"Buffer\" error from camera: %s\n",
                             pPerCameraMgr->name);
                break;

            default:
                M_ERROR("Camera: %s Framenumber: %d ErrorCode: %d\n", pPerCameraMgr->name,
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
        M_ERROR("%s was given NULL pointer for image\n", __FUNCTION__);
        throw -EINVAL;
    }

    //check the row that is 4/5ths of the way down the image, if we just converted a
    //raw8 image to raw8, it will be empty
    uint8_t* row = &(pImg[(heightPixels * widthPixels)]);
    for(unsigned int i = 0; i < widthPixels; i++){
        if(row[i] != 0){
            return true;
        }
    }
    return false;
}

static void reverse(uint8_t *mem, int size)
{

    uint8_t buffer;

    for(int i = 0; i < size/2; i++){

        buffer = mem[i];
        mem[i] = mem[size - i];
        mem[size - i] = buffer;

    }
}

// Given a file path, create all constituent directories if missing
static void CreateParentDirs(const char *file_path)
{
  char *dir_path = (char *) malloc(strlen(file_path) + 1);
  const char *next_sep = strchr(file_path, '/');
  while (next_sep != NULL) {
    int dir_path_len = next_sep - file_path;
    memcpy(dir_path, file_path, dir_path_len);
    dir_path[dir_path_len] = '\0';
    mkdir(dir_path, S_IRWXU|S_IRWXG|S_IROTH);
    next_sep = strchr(next_sep + 1, '/');
  }
  free(dir_path);
}

size_t find_jpeg_buffer_size(const uint8_t* buffer, size_t buffersize) {
    size_t jpeg_start = 0;
    size_t jpeg_end = 0;

    for (size_t i = 0; i < buffersize - 1; i++) {
        // Search for the start of a JPEG file (FF D8).
        if (buffer[i] == 0xFF && buffer[i+1] == 0xD8) {
            jpeg_start = i;
        }
        // If we've found the start of a JPEG file, search for the end (FF D9).
        else if (buffer[i] == 0xFF && buffer[i+1] == 0xD9) {
            jpeg_end = i + 2;
        }
    }
    return jpeg_end - jpeg_start;
}

static void WriteSnapshot(BufferBlock* bufferBlockInfo, int format, const char* path)
{
    uint64_t size    = bufferBlockInfo->size;
    uint8_t* src_data = (uint8_t*)bufferBlockInfo->vaddress;
    int extractJpgSize = find_jpeg_buffer_size(src_data, size);

    FILE* file_descriptor = fopen(path, "wb");
    if(! file_descriptor){

        //Check to see if we were just missing parent directories
        CreateParentDirs(path);

        file_descriptor = fopen(path, "wb");

        if(! file_descriptor){
            M_ERROR("failed to open file descriptor for snapshot save\n");
            return;
        }
    }

    if (format == HAL_PIXEL_FORMAT_BLOB) {

        fwrite(src_data, extractJpgSize, 1, file_descriptor);

    } else {
        M_ERROR("%s recieved frame in unsuppored format\n", __FUNCTION__);
    }

    fclose(file_descriptor);
}

static void Mipi12ToRaw16(camera_image_metadata_t meta, uint8_t *raw12Buf, uint16_t *raw16Buf)
{
    // Convert image buffer from MIPI RAW12 to RAW16 format
    // ToF MIPI RAW12 is stored in the format of:
    // P1[11:4] P2[11:4] P2[3:0] P1[3:0]
    // 2 pixels occupy 3 bytes, no padding needed

    // ToF data is 12bit held in an 8bit format, use seperate counter to track 1.5x difference in data size
    int buf8Idx, buf16Idx;
    for (buf8Idx = 0, buf16Idx = 0; buf16Idx < meta.size_bytes / 2; buf8Idx += 3, buf16Idx++){
        raw16Buf[(buf16Idx*2)]   = (raw12Buf[buf8Idx] << 4) + (raw12Buf[buf8Idx + 2] & 0x0F);
        raw16Buf[(buf16Idx*2)+1] = (raw12Buf[buf8Idx + 1] << 4) + ((raw12Buf[buf8Idx + 2] & 0xF0) >> 4);
    }
}


void PerCameraMgr::ProcessPreviewFrame(image_result result)
{
    BufferBlock* bufferBlockInfo = bufferGetBufferInfo(&pre_bufferGroup, result.second.buffer);

    camera_image_metadata_t imageInfo;
    if(getMeta(result.first, &imageInfo)) {
        M_WARN("Trying to process encode buffer without metadata\n");
        // bufferPush(rec_bufferGroup, bufferBlockInfo);
        return;
    }

    imageInfo.magic_number = CAMERA_MAGIC_NUMBER;
    imageInfo.width        = pre_width;
    imageInfo.height       = pre_height;


    //Tof is different from the rest, pass the data off to spectre then send it out
    if(configInfo.type == CAMTYPE_TOF) {
        tofFrameCounter++;
        if(standby_active && tofFrameCounter % (int)configInfo.decimator != 0){
            return;
        }
        #ifdef APQ8096
            TOFProcessRAW16(tof_interface,
                        (uint16_t *)bufferBlockInfo->vaddress,
                        imageInfo.timestamp_ns);
        #elif QRB5165

            uint16_t srcPixel16[pre_width * pre_height] = {0};
            imageInfo.format     = IMAGE_FORMAT_RAW8;
            imageInfo.size_bytes = pre_width * pre_height;
            imageInfo.stride     = pre_width;

            // TODO: instead of creating new array you can do this raw12->raw16 cvt in place
            Mipi12ToRaw16(imageInfo, (uint8_t *)bufferBlockInfo->vaddress, srcPixel16);
            tof_interface->ProcessRAW16(srcPixel16, imageInfo.timestamp_ns);
        #endif
        M_VERBOSE("Sent tof data to royale for processing\n");
        return;
    }


    if (pre_halfmt == HAL_PIXEL_FORMAT_RAW10)
    {
        M_VERBOSE("Preview format HAL_PIXEL_FORMAT_RAW10\n");

        // check the first frame to see if we actually got a raw10 frame or if it's actually raw8
        if(imageInfo.frame_id == 1){
            M_DEBUG("%s received raw10 frame, checking to see if is actually raw8\n", name);

            if((is10bit = Check10bit((uint8_t*)bufferBlockInfo->vaddress, pre_width, pre_height))){
                M_WARN("Recieved RAW10 frame, will be converting to RAW8 on cpu\n");
            } else {
                M_DEBUG("Frame was actually 8 bit, sending as is\n");
            }
        }

        imageInfo.format     = IMAGE_FORMAT_RAW8;
        imageInfo.size_bytes = pre_width * pre_height;
        imageInfo.stride     = pre_width;

        if(is10bit){
            ConvertTo8bitRaw((uint8_t*)bufferBlockInfo->vaddress,
                             pre_width,
                             pre_height);
        }
    }
    else if (pre_halfmt == HAL3_FMT_YUV)
    {
        imageInfo.format     = IMAGE_FORMAT_NV12;
        // no need to make contiguous anymore, better method with pipe_server_write_list
        // bufferMakeYUVContiguous(bufferBlockInfo);
        ///<@todo assuming 420 format and multiplying by 1.5 because NV21/NV12 is 12 bits per pixel
        imageInfo.size_bytes = (bufferBlockInfo->width * bufferBlockInfo->height * 1.5);

    } else {
        M_ERROR("Camera: %s received invalid preview format, stopping\n", name);
        EStopCameraServer();
    }

    if (partnerMode == MODE_MONO){

        if(pre_halfmt == HAL_PIXEL_FORMAT_RAW10){
            // Ship the frame out of the camera server
            pipe_server_write_camera_frame(outputChannel, imageInfo, bufferBlockInfo->vaddress);
        }
        else if (pre_halfmt == HAL3_FMT_YUV){
            size_t ylen = bufferBlockInfo->width * bufferBlockInfo->height;
            size_t uvlen = ylen/2;
            const void* bufs[] = {&imageInfo, bufferBlockInfo->vaddress, bufferBlockInfo->uvHead};
            size_t lens[] = {sizeof(camera_image_metadata_t), ylen, uvlen};
            pipe_server_write_list(outputChannel, 3, bufs, lens);
        }
        M_VERBOSE("Sent frame %d through pipe %s\n", imageInfo.frame_id, name);

        int64_t    new_exposure_ns;
        int32_t    new_gain;

        pthread_mutex_lock(&aeMutex);
        if (ae_mode == AE_LME_HIST && expHistInterface.update_exposure(
                (uint8_t*)bufferBlockInfo->vaddress,
                pre_width,
                pre_height,
                imageInfo.exposure_ns,
                imageInfo.gain,
                &new_exposure_ns,
                &new_gain)){

            setExposure = new_exposure_ns;
            setGain     = new_gain;
        }

        if (ae_mode == AE_LME_MSV && expMSVInterface.update_exposure(
                (uint8_t*)bufferBlockInfo->vaddress,
                pre_width,
                pre_height,
                imageInfo.exposure_ns,
                imageInfo.gain,
                &new_exposure_ns,
                &new_gain)){

            setExposure = new_exposure_ns;
            setGain     = new_gain;
        }
        pthread_mutex_unlock(&aeMutex);

    } else if (partnerMode == MODE_STEREO_MASTER){

        switch (imageInfo.format){
            case IMAGE_FORMAT_NV12:
                imageInfo.format = IMAGE_FORMAT_STEREO_NV12;
                imageInfo.size_bytes = pre_width * pre_height * 1.5 * 2;
                break;
            case IMAGE_FORMAT_RAW8:
                imageInfo.format = IMAGE_FORMAT_STEREO_RAW8;
                imageInfo.size_bytes = pre_width * pre_height * 2;
                break;
            case IMAGE_FORMAT_STEREO_RAW8:
            case IMAGE_FORMAT_STEREO_NV21:
                break;
            default:
                M_ERROR("libmodal-pipe does not support stereo pairs in formats other than NV12 or RAW8: %s\n", pipe_image_format_to_string(imageInfo.format));
                EStopCameraServer();
                break;
        }

        NEED_CHILD:
        pthread_mutex_lock(&stereoMutex);
        if(childFrame == NULL){
            pthread_cond_wait(&stereoCond, &stereoMutex);
        }

        if(EStopped | stopped) {
            pthread_cond_signal(&(otherMgr->stereoCond));
            pthread_mutex_unlock(&stereoMutex);
            return;
        }

        if(childFrame == NULL){
            pthread_mutex_unlock(&stereoMutex);
            M_WARN("Child frame not received, assuming missing and discarding master\n");
            return;
        }

        int64_t diff = imageInfo.timestamp_ns - childInfo.timestamp_ns;
        M_VERBOSE("%s timestamps(ms): %llu, %llu, diff: %lld\n",
            name,
            imageInfo.timestamp_ns/1000000,
            childInfo.timestamp_ns/1000000,
            diff/1000000);

        //Much newer master, discard the child and get a new one
        if(diff > MAX_STEREO_DISCREPENCY_NS){
            M_WARN("Camera %s recieved much newer master than child (%lld), discarding child and trying again\n",
                name, diff/1000000);
            childFrame = NULL;
            pthread_mutex_unlock(&stereoMutex);
            pthread_cond_signal(&(otherMgr->stereoCond));
            goto NEED_CHILD;
        }

        diff *= -1;
        //Much newer child, discard master but keep the child
        if(diff > MAX_STEREO_DISCREPENCY_NS){
            M_WARN("Camera %s recieved much newer child than master (%lld), discarding master and trying again\n",
                name, diff/1000000);
            pthread_mutex_unlock(&stereoMutex);
            return;
        }

        // Assume the earlier timestamp is correct
        if(imageInfo.timestamp_ns > childInfo.timestamp_ns){
            imageInfo.timestamp_ns = childInfo.timestamp_ns;
        }

        // Ship the frame out of the camera server
        // pipe_server_write_stereo_frame(outputChannel, imageInfo, bufferBlockInfo->vaddress, childFrame);
        // M_VERBOSE("Sent frame %d through pipe %s\n", imageInfo.frame_id, name);

        if(pre_halfmt == HAL_PIXEL_FORMAT_RAW10)
        {
            size_t len = bufferBlockInfo->width * bufferBlockInfo->height;
            const void* bufs[] = {&imageInfo, bufferBlockInfo->vaddress, childFrame};
            size_t lens[] = {sizeof(camera_image_metadata_t), len, len};
            pipe_server_write_list(outputChannel, 3, bufs, lens);
        }
        else if (pre_halfmt == HAL3_FMT_YUV)
        {
            size_t ylen = bufferBlockInfo->width * bufferBlockInfo->height;
            size_t uvlen = ylen/2;
            const void* bufs[] = {  &imageInfo,
                                    bufferBlockInfo->vaddress,
                                    bufferBlockInfo->uvHead,
                                    childFrame,
                                    childFrame_uvHead};
            size_t lens[] = {sizeof(camera_image_metadata_t), ylen, uvlen, ylen, uvlen};
            pipe_server_write_list(outputChannel, 5, bufs, lens);
        }

        M_VERBOSE("Sent frame %d through pipe %s\n", imageInfo.frame_id, name);

        // Run Auto Exposure
        int64_t    new_exposure_ns;
        int32_t    new_gain;

        pthread_mutex_lock(&aeMutex);
        if (ae_mode == AE_LME_HIST && expHistInterface.update_exposure(
                (uint8_t*)bufferBlockInfo->vaddress,
                pre_width,
                pre_height,
                imageInfo.exposure_ns,
                imageInfo.gain,
                &new_exposure_ns,
                &new_gain)){

            setExposure = new_exposure_ns;
            setGain     = new_gain;

            if(!configInfo.ind_exp) {
                //Pass back the new AE values to the other camera
                otherMgr->setExposure = new_exposure_ns;
                otherMgr->setGain = new_gain;
            }
        }

        if (ae_mode == AE_LME_MSV && expMSVInterface.update_exposure(
                (uint8_t*)bufferBlockInfo->vaddress,
                pre_width,
                pre_height,
                imageInfo.exposure_ns,
                imageInfo.gain,
                &new_exposure_ns,
                &new_gain)){

            setExposure = new_exposure_ns;
            setGain     = new_gain;

            if(!configInfo.ind_exp) {
                //Pass back the new AE values to the other camera
                otherMgr->setExposure = new_exposure_ns;
                otherMgr->setGain = new_gain;
            }
        }
        pthread_mutex_unlock(&aeMutex);

        //Clear the pointers and signal the child thread for cleanup
        childFrame = NULL;
        pthread_mutex_unlock(&stereoMutex);
        pthread_cond_signal(&(otherMgr->stereoCond));

    } else if (partnerMode == MODE_STEREO_SLAVE){

        pthread_mutex_lock(&(otherMgr->stereoMutex));

        otherMgr->childFrame = (uint8_t*)bufferBlockInfo->vaddress;
        otherMgr->childFrame_uvHead = (uint8_t*)bufferBlockInfo->uvHead;
        otherMgr->childInfo  = imageInfo;

        pthread_cond_signal(&(otherMgr->stereoCond));
        pthread_cond_wait(&stereoCond, &(otherMgr->stereoMutex));
        pthread_mutex_unlock(&(otherMgr->stereoMutex));

        if(configInfo.ind_exp) {
            // Run Auto Exposure
            int64_t    new_exposure_ns;
            int32_t    new_gain;

            pthread_mutex_lock(&aeMutex);
            if (ae_mode == AE_LME_HIST && expHistInterface.update_exposure(
                    (uint8_t*)bufferBlockInfo->vaddress,
                    pre_width,
                    pre_height,
                    imageInfo.exposure_ns,
                    imageInfo.gain,
                    &new_exposure_ns,
                    &new_gain)){

                setExposure = new_exposure_ns;
                setGain     = new_gain;
            }

            if (ae_mode == AE_LME_MSV && expMSVInterface.update_exposure(
                    (uint8_t*)bufferBlockInfo->vaddress,
                    pre_width,
                    pre_height,
                    imageInfo.exposure_ns,
                    imageInfo.gain,
                    &new_exposure_ns,
                    &new_gain)){

                setExposure = new_exposure_ns;
                setGain     = new_gain;
            }

            pthread_mutex_unlock(&aeMutex);

        }

    }

}

void PerCameraMgr::ProcessStreamFrame(image_result result)
{

    BufferBlock* bufferBlockInfo = bufferGetBufferInfo(&str_bufferGroup, result.second.buffer);

    camera_image_metadata_t meta;
    if(getMeta(result.first, &meta)) {
        M_WARN("Trying to process record buffer without metadata\n");
        bufferPush(str_bufferGroup, result.second.buffer);
        return;
    }

    meta.width =  str_width;
    meta.height = str_height;

    // check health of the encoder and drop this frame if it's getting backed up
    int n = pVideoEncoderStream->ItemsInQueue();
    if(n>STREAM_ALLOWED_ITEMS_IN_OMX_QUEUE){
        M_WARN("dropping stream frame, OMX is getting backed up, has %d in queue already\n", n);
        bufferPush(str_bufferGroup, result.second.buffer);
        return;
    }

    // add to the OMX queue
    pVideoEncoderStream->ProcessFrameToEncode(meta, bufferBlockInfo);

}

void PerCameraMgr::ProcessRecordFrame(image_result result)
{

    BufferBlock* bufferBlockInfo = bufferGetBufferInfo(&rec_bufferGroup, result.second.buffer);

    camera_image_metadata_t meta;
    if(getMeta(result.first, &meta)) {
        M_WARN("Trying to process record buffer without metadata\n");
        bufferPush(rec_bufferGroup, result.second.buffer);
        return;
    }

    meta.width = rec_width;
    meta.height = rec_height;

    // check health of the encoder and drop this frame if it's getting backed up
    int n = pVideoEncoderRecord->ItemsInQueue();
    if(n>RECORD_ALLOWED_ITEMS_IN_OMX_QUEUE){
        M_WARN("dropping record frame, OMX is getting backed up, has %d in queue already\n", n);
        bufferPush(rec_bufferGroup, result.second.buffer);
        return;
    }

    // add to the OMX queue
    pVideoEncoderRecord->ProcessFrameToEncode(meta, bufferBlockInfo);

}

void PerCameraMgr::ProcessSnapshotFrame(image_result result)
{

    BufferBlock* bufferBlockInfo = bufferGetBufferInfo(&snap_bufferGroup, result.second.buffer);

    if(snapshotQueue.size() != 0){
        char *filename = snapshotQueue.front();
        snapshotQueue.pop_front();

        M_PRINT("Camera: %s writing snapshot to :\"%s\"\n", name, filename);
        WriteSnapshot(bufferBlockInfo, snap_halfmt, filename);

        free(filename);
    }

}

// -----------------------------------------------------------------------------------------------------------------------------
// The TOF library calls this function when it receives data from the Royale PMD libs
// -----------------------------------------------------------------------------------------------------------------------------
// Callback function for the TOF bridge to provide the post processed TOF data
bool PerCameraMgr::RoyaleDataDone(const void*             pData,
                                  uint32_t                size,
                                  int64_t                 timestamp,
                                  RoyaleListenerType      dataType)
{

    M_VERBOSE("Received royale data for camera: %s\n", name);

    constexpr int MAX_IR_VALUE_IN  = 2895;
    constexpr int MAX_IR_VALUE_OUT = (1<<8);

    const royale::DepthData* pDepthData               = static_cast<const royale::DepthData *> (pData);
    const royale::Vector<royale::DepthPoint>& pointIn = pDepthData->points;
    int numPoints = (int)pointIn.size();

    camera_image_metadata_t IRMeta, DepthMeta, ConfMeta;
    point_cloud_metadata_t PCMeta;

    // set up some common metadata
    IRMeta.timestamp_ns = pDepthData->timeStamp.count();
    IRMeta.gain         = 0;
    IRMeta.exposure_ns  = 0;
    IRMeta.frame_id     = ++TOFFrameNumber;
    IRMeta.width        = pDepthData->width;
    IRMeta.height       = pDepthData->height;
    DepthMeta = IRMeta;
    ConfMeta  = IRMeta;

    if(pipe_server_get_num_clients(IROutputChannel)>0){
        IRMeta.stride         = IRMeta.width * sizeof(uint8_t);
        IRMeta.size_bytes     = IRMeta.stride * IRMeta.height;
        IRMeta.format         = IMAGE_FORMAT_RAW8;
        uint8_t IRData[numPoints];
        for (int i = 0; i < numPoints; i++)
        {
            royale::DepthPoint point = pointIn[i];
            uint32_t longval = point.grayValue;
            longval *= MAX_IR_VALUE_OUT;
            longval /= MAX_IR_VALUE_IN;
            IRData[i]    = longval;
        }
        pipe_server_write_camera_frame(IROutputChannel, IRMeta, IRData);
    }

    if(pipe_server_get_num_clients(DepthOutputChannel)>0){
        DepthMeta.stride      = DepthMeta.width * sizeof(uint8_t);
        DepthMeta.size_bytes  = DepthMeta.stride * DepthMeta.height;
        DepthMeta.format      = IMAGE_FORMAT_RAW8;
        uint8_t DepthData[numPoints];
        for (int i = 0; i < numPoints; i++)
        {
            DepthData[i] = (uint8_t)((pointIn[i].z / 5) * 255);
        }
        pipe_server_write_camera_frame(DepthOutputChannel, DepthMeta, DepthData);
    }

    if(pipe_server_get_num_clients(ConfOutputChannel)>0){
        ConfMeta.stride       = ConfMeta.width * sizeof(uint8_t);
        ConfMeta.size_bytes   = ConfMeta.stride * ConfMeta.height;
        ConfMeta.format       = IMAGE_FORMAT_RAW8;
        uint8_t ConfData[numPoints];
        for (int i = 0; i < numPoints; i++)
        {
            royale::DepthPoint point = pointIn[i];
            ConfData[i] = point.depthConfidence;
        }
        pipe_server_write_camera_frame(ConfOutputChannel, ConfMeta, ConfData);
    }

    if(pipe_server_get_num_clients(PCOutputChannel)>0){
        PCMeta.timestamp_ns   = IRMeta.timestamp_ns;
        PCMeta.n_points       = numPoints;
        float PointCloud[numPoints*3];
        for (int i = 0; i < numPoints; i++)
        {
            royale::DepthPoint point = pointIn[i];
            PointCloud[(i*3)]   = point.x;
            PointCloud[(i*3)+1] = point.y;
            PointCloud[(i*3)+2] = point.z;
        }
        pipe_server_write_point_cloud(PCOutputChannel, PCMeta, PointCloud);
    }

    if(pipe_server_get_num_clients(FullOutputChannel)>0){
        tof_data_t FullData;
        FullData.magic_number = TOF_MAGIC_NUMBER;
        FullData.timestamp_ns = IRMeta.timestamp_ns;
        for (int i = 0; i < numPoints; i++)
        {
            royale::DepthPoint point = pointIn[i];
            FullData.points     [i][0] = point.x;
            FullData.points     [i][1] = point.y;
            FullData.points     [i][2] = point.z;
            FullData.noises     [i]    = point.noise;
            uint32_t longval = point.grayValue;
            longval *= MAX_IR_VALUE_OUT;
            longval /= MAX_IR_VALUE_IN;
            FullData.grayValues [i]    = longval;
            FullData.confidences[i]    = point.depthConfidence;
        }
        pipe_server_write(FullOutputChannel, (const char *)(&FullData), sizeof(tof_data_t));
    }

    return true;
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
        M_VERBOSE("Entered thread: %s(tid: %lu)\n", buf, tid);

        // Set thread priority
        int which = PRIO_PROCESS;
        int nice  = -10;
        setpriority(which, tid, nice);
    }

    uint8_t num_finished_streams = en_snapshot;

    // The condition of the while loop is such that this thread will not terminate till it receives the last expected image
    // frame from the camera module or detects the ESTOP flag
    while (!EStopped && num_finished_streams != num_streams)
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

        image_result result = resultMsgQueue.front();
        resultMsgQueue.pop_front();
        pthread_mutex_unlock(&resultMutex);

        buffer_handle_t  *handle      = result.second.buffer;
        camera3_stream_t *stream      = result.second.stream;
        BufferGroup      *bufferGroup = GetBufferGroup(stream);


        // Coming here means we have a result frame to process
        M_VERBOSE("%s procesing new buffer\n", name);

        switch (GetStreamId(stream)){
            case STREAM_PREVIEW:
                M_VERBOSE("Camera: %s processing preview frame\n", name);
                ProcessPreviewFrame(result);
                bufferPush(*bufferGroup, handle); // This queues up the buffer for recycling
                break;

            case STREAM_STREAM: // Not Ready
                M_VERBOSE("Camera: %s processing stream frame\n", name);
                ProcessStreamFrame(result);
                break;

            case STREAM_RECORD: // Not Ready
                M_VERBOSE("Camera: %s processing record frame\n", name);
                ProcessRecordFrame(result);
                break;

            case STREAM_SNAPSHOT:
                M_VERBOSE("Camera: %s processing snapshot frame\n", name);
                ProcessSnapshotFrame(result);
                bufferPush(*bufferGroup, handle); // This queues up the buffer for recycling
                break;

            default:
                M_ERROR("Camera: %s recieved frame for unknown stream\n", name);
                bufferPush(*bufferGroup, handle); // This queues up the buffer for recycling
                break;
        }

        if (lastResultFrameNumber == result.first)
            num_finished_streams++;



    }

    if(EStopped){
        M_WARN("Thread: %s result thread recieved ESTOP\n", name);
    }else{
        M_DEBUG("------ Last %s result frame: %d\n", name, lastResultFrameNumber);
    }

    M_VERBOSE("Leaving %s result thread\n", name);

    return NULL;
}


int PerCameraMgr::HasClientForPreviewFrame()
{
    if(configInfo.type == CAMTYPE_TOF){
        if(pipe_server_get_num_clients(IROutputChannel   )>0) return 1;
        if(pipe_server_get_num_clients(DepthOutputChannel)>0) return 1;
        if(pipe_server_get_num_clients(ConfOutputChannel )>0) return 1;
        if(pipe_server_get_num_clients(PCOutputChannel   )>0) return 1;
        if(pipe_server_get_num_clients(FullOutputChannel )>0) return 1;
    }
    else{
        if(pipe_server_get_num_clients(outputChannel)>0) return 1;
    }
    return 0;
}


// -----------------------------------------------------------------------------------------------------------------------------
// Send one capture request to the camera module
// -----------------------------------------------------------------------------------------------------------------------------
int PerCameraMgr::ProcessOneCaptureRequest(int frameNumber)
{
    camera3_capture_request_t request;

    if(ae_mode != AE_ISP){
        requestMetadata.update(ANDROID_SENSOR_EXPOSURE_TIME, &setExposure, 1);
        requestMetadata.update(ANDROID_SENSOR_SENSITIVITY,   &setGain, 1);
    }

    std::vector<camera3_stream_buffer_t> streamBufferList;
    request.num_output_buffers  = 0;

    if(en_stream && pipe_server_get_num_clients(streamOutputChannel)>0){

        int nFree = bufferNumFree(str_bufferGroup);
        if(nFree<1){
            M_WARN("Stream stream buffer pool for Cam(%s), Frame(%d) has %d free, skipping request\n", name, frameNumber, nFree);
        }
        else{

            camera3_stream_buffer_t estreamBuffer;
            if ((estreamBuffer.buffer   = (const native_handle_t**)bufferPop(str_bufferGroup)) == NULL) {
                M_ERROR("Failed to get buffer for stream stream: Cam(%s), Frame(%d)\n", name, frameNumber);
                EStopCameraServer();
                return -1;
            }

            estreamBuffer.stream        = &str_stream;
            estreamBuffer.status        = 0;
            estreamBuffer.acquire_fence = -1;
            estreamBuffer.release_fence = -1;

            request.num_output_buffers ++;
            streamBufferList.push_back(estreamBuffer);
        }
    }

    if(en_record && pipe_server_get_num_clients(recordOutputChannel)>0){

        int nFree = bufferNumFree(rec_bufferGroup);
        if(nFree<1){
            M_WARN("record stream buffer pool for Cam(%s), Frame(%d) has %d free, skipping request\n", name, frameNumber, nFree);
        }
        else{
            camera3_stream_buffer_t rstreamBuffer;
            if ((rstreamBuffer.buffer   = (const native_handle_t**)bufferPop(rec_bufferGroup)) == NULL) {
                M_ERROR("Failed to get buffer for record stream: Cam(%s), Frame(%d)\n", name, frameNumber);
                EStopCameraServer();
                return -1;
            }

            rstreamBuffer.stream        = &rec_stream;
            rstreamBuffer.status        = 0;
            rstreamBuffer.acquire_fence = -1;
            rstreamBuffer.release_fence = -1;

            request.num_output_buffers ++;
            streamBufferList.push_back(rstreamBuffer);
        }
    }

    if(en_snapshot && numNeededSnapshots > 0){

        int nFree = bufferNumFree(snap_bufferGroup);
        if(nFree<1){
            M_WARN("snapshot buffer pool for Cam(%s), Frame(%d) has %d free, skipping request\n", name, frameNumber, nFree);
        }
        else{
            numNeededSnapshots --;

            camera3_stream_buffer_t sstreamBuffer;
            if((sstreamBuffer.buffer    = (const native_handle_t**)bufferPop(snap_bufferGroup)) == NULL) {
                M_ERROR("Failed to get buffer for snapshot stream: Cam(%s), Frame(%d)\n", name, frameNumber);
                EStopCameraServer();
                return -1;
            }
            sstreamBuffer.stream        = &snap_stream;
            sstreamBuffer.status        = 0;
            sstreamBuffer.acquire_fence = -1;
            sstreamBuffer.release_fence = -1;

            request.num_output_buffers ++;
            streamBufferList.push_back(sstreamBuffer);
        }

    }

    if(  HasClientForPreviewFrame() ||
        (ae_mode == AE_ISP && !request.num_output_buffers) ||
        (ae_mode != AE_OFF && ae_mode != AE_ISP))
    {
        int nFree = bufferNumFree(pre_bufferGroup);
        if(nFree<1){
            M_WARN("preview buffer pool for Cam(%s), Frame(%d) has %d free, skipping request\n", name, frameNumber, nFree);
        }
        else{
            camera3_stream_buffer_t pstreamBuffer;
            if((pstreamBuffer.buffer    = (const native_handle_t**)bufferPop(pre_bufferGroup)) == NULL) {
                M_ERROR("Failed to get buffer for preview stream: Cam(%s), Frame(%d)\n", name, frameNumber);
                EStopCameraServer();
                return -1;
            }
            pstreamBuffer.stream        = &pre_stream;
            pstreamBuffer.status        = 0;
            pstreamBuffer.acquire_fence = -1;
            pstreamBuffer.release_fence = -1;

            request.num_output_buffers ++;
            streamBufferList.push_back(pstreamBuffer);
        }
    }

    request.output_buffers      = streamBufferList.data();
    request.frame_number        = frameNumber;
    request.settings            = requestMetadata.getAndLock();
    request.input_buffer        = nullptr;

    // If there are no output buffers just do nothing
    // Without this an illigal zero output buffer request will be made
    if (request.num_output_buffers == 0){
        // Output buffers are full delay the next request
        // Without this wait at high CPU loads the loop will runn away with CPU usage
        usleep(10000);
        return S_OK;
    }

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
    M_VERBOSE("Sending request for frame %d for camera %s\n", frameNumber, name);

    if (int status = pDevice->ops->process_capture_request(pDevice, &request))
    {

        //Another thread has already detected the fatal error, return since it has already been handled
        if(stopped) return 0;

        M_ERROR("Recieved Fatal error from camera: %s\n", name);
        switch (status){
            case -EINVAL :
                M_ERROR("Sending request %d, ErrorCode: -EINVAL\n", frameNumber);
                break;
            case -ENODEV:
                M_ERROR("Sending request %d, ErrorCode: -ENODEV\n", frameNumber);
                break;
            default:
                M_ERROR("Sending request %d, ErrorCode: %d\n", frameNumber, status);
                break;
        }

        EStopCameraServer();
        return -EINVAL;
    }

    M_VERBOSE("finished sending request for frame %d for camera %s\n", frameNumber, name);

    requestMetadata.unlock(request.settings);

    M_VERBOSE("returning from ProcessOneCaptureRequest for frame %d for camera %s\n", frameNumber, name);

    return S_OK;
}

// -----------------------------------------------------------------------------------------------------------------------------
// Main thread function to initiate the sending of capture requests to the camera module. Keeps on sending the capture requests
// to the camera module till a "stop message" is passed to this thread function
// -----------------------------------------------------------------------------------------------------------------------------
void* PerCameraMgr::ThreadIssueCaptureRequests()
{

    uint32_t frame_number = 0;
    char buf[16];
    sprintf(buf, "cam%d-request", cameraId);
    pthread_setname_np(pthread_self(), buf);

    M_VERBOSE("Entered thread: %s(tid: %lu)\n", buf, syscall(SYS_gettid));

    if (ConstructDefaultRequestSettings()){

        M_ERROR("Failed to construct request settings for camera: %s\n", name);

        EStopCameraServer();
    }

    while (!stopped && !EStopped)
    {

        /** This is an old TODO comment, I think ProcessOneCaptureRequest handles
          * this now, but leaving the comment here in case it misbahves and better
          * behavior is needed in the future
                    // if(!getNumClients() && !numNeededSnapshots){
                    //     //TODODODODODODOD THIS NEEDS TO BE A COND SLEEP
                    //     usleep(100000);
                    //     if(stopped || EStopped) break;
                    // }
        **/
        ProcessOneCaptureRequest(++frame_number);
    }

    // Stop message received. Inform about the last framenumber requested from the camera module. This in turn will be used
    // by the result thread to wait for this frame's image buffers to arrive.
    if(EStopped){
        M_WARN("Thread: %s request thread recieved ESTOP\n", name);
    }else{
        lastResultFrameNumber = frame_number;
        M_DEBUG("------ Last request frame for %s: %d\n", name, frame_number);
    }

    M_VERBOSE("Leaving %s request thread\n", name);

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
static const char* CmdStrings[] =
{
    "set_exp_gain",
    "set_exp",
    "set_gain",
    "start_ae",
    "stop_ae",
    "snapshot"
};

int PerCameraMgr::SetupPipes()
{
    if(configInfo.type != CAMTYPE_TOF){
        //Set up the control callback (wrapped in a lambda because it's a member function)
        pipe_server_set_control_cb(
                outputChannel,                                         //Channel
                [](int ch, char * string, int bytes, void* context)    //Callback
                        {((PerCameraMgr*)context)->HandleControlCmd(string);},
                this);                                                 //Context

        char cont_cmds[256];
        snprintf(cont_cmds, 255, "%s%s",
            CAM_CONTROL_COMMANDS,
            en_snapshot ? ",snapshot" : "");

        pipe_info_t info;
        strcpy(info.name       , name);
        strcpy(info.type       , "camera_image_metadata_t");
        strcpy(info.server_name, PROCESS_NAME);
        info.size_bytes = 64*1024*1024;

        pipe_server_create(outputChannel, info, SERVER_FLAG_EN_CONTROL_PIPE);

        pipe_server_set_available_control_commands(outputChannel, cont_cmds);

        if(en_stream){
            char encode_name[32];
            snprintf(encode_name, 31, "%s_stream", name);
            strcpy(info.name, encode_name);
            pipe_server_create(streamOutputChannel, info, 0);
        }

        if(en_record){
            char encode_name[32];
            snprintf(encode_name, 31, "%s_record", name);
            strcpy(info.name, encode_name);
            pipe_server_create(recordOutputChannel, info, 0);
        }

    } else {

        IROutputChannel    = outputChannel;
        DepthOutputChannel = pipe_server_get_next_available_channel();
        ConfOutputChannel  = pipe_server_get_next_available_channel();
        PCOutputChannel    = pipe_server_get_next_available_channel();
        FullOutputChannel  = pipe_server_get_next_available_channel();

        pipe_info_t IRInfo;
        pipe_info_t DepthInfo;
        pipe_info_t ConfInfo;
        pipe_info_t PCInfo;
        pipe_info_t FullInfo;

        sprintf(IRInfo.name,    "%s%s", name, "_ir");
        sprintf(DepthInfo.name, "%s%s", name, "_depth");
        sprintf(ConfInfo.name,  "%s%s", name, "_conf");
        sprintf(PCInfo.name,    "%s%s", name, "_pc");
        sprintf(FullInfo.name,  "%s%s", name, "");

        strcpy(IRInfo.type,     "camera_image_metadata_t");
        strcpy(DepthInfo.type,  "camera_image_metadata_t");
        strcpy(ConfInfo.type,   "camera_image_metadata_t");
        strcpy(PCInfo.type,     "point_cloud_metadata_t");
        strcpy(FullInfo.type,   "tof_data_t");

        strcpy(IRInfo.server_name,    PROCESS_NAME);
        strcpy(DepthInfo.server_name, PROCESS_NAME);
        strcpy(ConfInfo.server_name,  PROCESS_NAME);
        strcpy(PCInfo.server_name,    PROCESS_NAME);
        strcpy(FullInfo.server_name,  PROCESS_NAME);

        IRInfo.size_bytes    = 64*1024*1024;
        DepthInfo.size_bytes = 64*1024*1024;
        ConfInfo.size_bytes  = 64*1024*1024;
        PCInfo.size_bytes    = 64*1024*1024;
        FullInfo.size_bytes  = 256*1024*1024;

        pipe_server_create(IROutputChannel,    IRInfo,    0);
        pipe_server_create(DepthOutputChannel, DepthInfo, 0);
        pipe_server_create(ConfOutputChannel,  ConfInfo,  0);
        pipe_server_create(PCOutputChannel,    PCInfo,    0);
        pipe_server_create(FullOutputChannel,  FullInfo,  0);

    }
    return S_OK;
}

static int _exists(char* path)
{
    // file exists
    if(access(path, F_OK ) != -1 ) return 1;
    // file doesn't exist
    return 0;
}

void PerCameraMgr::HandleControlCmd(char* cmd)
{

    const float MIN_EXP  = ((float)configInfo.ae_hist_info.exposure_min_us)/1000;
    const float MAX_EXP  = ((float)configInfo.ae_hist_info.exposure_max_us)/1000;
    const int MIN_GAIN = configInfo.ae_hist_info.gain_min;
    const int MAX_GAIN = configInfo.ae_hist_info.gain_max;

    /**************************
     *
     * SET Exposure and Gain
     *
     */
    if(strncmp(cmd, CmdStrings[SET_EXP_GAIN], strlen(CmdStrings[SET_EXP_GAIN])) == 0){

        char buffer[strlen(CmdStrings[SET_EXP_GAIN])+1];
        float exp = -1.0;
        int gain = -1;

        if(sscanf(cmd, "%s %f %d", buffer, &exp, &gain) == 3){
            if(exp < MIN_EXP || exp > MAX_EXP){
                M_ERROR("Invalid Control Pipe Exposure: %f,\n\tShould be between %f and %f\n", exp, MIN_EXP, MAX_EXP);
            } else if(gain < MIN_GAIN || gain > MAX_GAIN){
                M_ERROR("Invalid Control Pipe Gain: %d,\n\tShould be between %d and %d\n", gain, MIN_GAIN, MAX_GAIN);
            } else {
                pthread_mutex_lock(&aeMutex);

                if(ae_mode != AE_OFF) {
                    ae_mode = AE_OFF;
                    ConstructDefaultRequestSettings();

                    if(otherMgr){
                        otherMgr->ae_mode = AE_OFF;
                        otherMgr->ConstructDefaultRequestSettings();
                    }
                }

                M_DEBUG("Camera: %s recieved new exp/gain values: %6.3f(ms) %d\n", name, exp, gain);

                setExposure = exp*1000000;
                setGain =     gain;

                if(otherMgr){
                    otherMgr->setExposure = exp*1000000;
                    otherMgr->setGain =     gain;
                }

                pthread_mutex_unlock(&aeMutex);
            }
        } else {
            M_ERROR("Camera: %s failed to get valid exposure/gain values from control pipe\n\tShould follow format: \"%s 25 350\"\n",
             name, CmdStrings[SET_EXP_GAIN]);
        }

    } else
    /**************************
     *
     * SET Exposure
     *
     */
    if(strncmp(cmd, CmdStrings[SET_EXP], strlen(CmdStrings[SET_EXP])) == 0){

        char buffer[strlen(CmdStrings[SET_EXP])+1];
        float exp = -1.0;

        if(sscanf(cmd, "%s %f", buffer, &exp) == 2){
            if(exp < MIN_EXP || exp > MAX_EXP){
                M_ERROR("Invalid Control Pipe Exposure: %f,\n\tShould be between %f and %f\n", exp, MIN_EXP, MAX_EXP);
            } else {
                pthread_mutex_lock(&aeMutex);

                if(ae_mode != AE_OFF) {
                    ae_mode = AE_OFF;
                    ConstructDefaultRequestSettings();

                    if(otherMgr){
                        otherMgr->ae_mode = AE_OFF;
                        otherMgr->ConstructDefaultRequestSettings();
                    }
                }

                M_DEBUG("Camera: %s recieved new exp value: %6.3f(ms)\n", name, exp);
                setExposure = exp*1000000;

                if(otherMgr){
                    otherMgr->setExposure = exp*1000000;
                }

                pthread_mutex_unlock(&aeMutex);
            }
        } else {
            M_ERROR("Camera: %s failed to get valid exposure value from control pipe\n\tShould follow format: \"%s 25\"\n",
            name, CmdStrings[SET_EXP]);
        }
    } else
    /**************************
     *
     * SET Gain
     *
     */
    if(strncmp(cmd, CmdStrings[SET_GAIN], strlen(CmdStrings[SET_GAIN])) == 0){

        char buffer[strlen(CmdStrings[SET_GAIN])+1];
        int gain = -1;

        if(sscanf(cmd, "%s %d", buffer, &gain) == 2){
            if(gain < MIN_GAIN || gain > MAX_GAIN){
                M_ERROR("Invalid Control Pipe Gain: %d,\n\tShould be between %d and %d\n", gain, MIN_GAIN, MAX_GAIN);
            } else {
                pthread_mutex_lock(&aeMutex);

                if(ae_mode != AE_OFF) {
                    ae_mode = AE_OFF;
                    ConstructDefaultRequestSettings();

                    if(otherMgr){
                        otherMgr->ae_mode = AE_OFF;
                        otherMgr->ConstructDefaultRequestSettings();
                    }
                }

                M_DEBUG("Camera: %s recieved new gain value: %d\n", name, gain);
                setGain = gain;

                if(otherMgr){
                    otherMgr->setGain = gain;
                }

                pthread_mutex_unlock(&aeMutex);
            }
        } else {
            M_ERROR("Camera: %s failed to get valid gain value from control pipe\n\tShould follow format: \"%s 350\"\n",
            name, CmdStrings[SET_GAIN]);
        }
    } else
    /**************************
     *
     * START Auto Exposure
     *
     */
    if(strncmp(cmd, CmdStrings[START_AE], strlen(CmdStrings[START_AE])) == 0){

        pthread_mutex_lock(&aeMutex);

        if(ae_mode != configInfo.ae_mode) {
            ae_mode = configInfo.ae_mode;
            ConstructDefaultRequestSettings();

            if(otherMgr){
                otherMgr->ae_mode = configInfo.ae_mode;
                otherMgr->ConstructDefaultRequestSettings();
            }

            M_DEBUG("Camera: %s starting to use Auto Exposure\n", name);
        }
        pthread_mutex_unlock(&aeMutex);

    } else
    /**************************
     *
     * STOP Auto Exposure
     *
     */
    if(strncmp(cmd, CmdStrings[STOP_AE], strlen(CmdStrings[STOP_AE])) == 0){

        pthread_mutex_lock(&aeMutex);

        if(ae_mode != AE_OFF) {
            ae_mode = AE_OFF;
            ConstructDefaultRequestSettings();

            if(otherMgr){
                otherMgr->ae_mode = AE_OFF;
                otherMgr->ConstructDefaultRequestSettings();
            }
            M_DEBUG("Camera: %s ceasing to use Auto Exposure\n", name);
        }
        pthread_mutex_unlock(&aeMutex);

    } else
    /**************************
     *
     * Take snapshot
     *
     */
    if(strncmp(cmd, CmdStrings[SNAPSHOT], strlen(CmdStrings[SNAPSHOT])) == 0){
        if(en_snapshot){
            char buffer[strlen(CmdStrings[SET_EXP_GAIN])+1];
            char *filename = (char *)malloc(256);

            if(sscanf(cmd, "%s %s", buffer, filename) != 2){
                // We weren't given a proper file, generate a default one
                // find next index open for that name. e/g/ hires-0, hires-1, hires-2...
                for(int i=lastSnapshotNumber;;i++){
                    // construct a new path with the current dir, name, and index i
                    sprintf(filename,"/data/snapshots/%s-%d.jpg", name, i);
                    if(!_exists(filename)){
                        // name with this index doesn't exist yet, good, use it!
                        lastSnapshotNumber = i;
                        break;
                    }
                }
            }

            M_PRINT("Camera: %s taking snapshot (destination: %s)\n", name, filename);

            snapshotQueue.push_back(filename);
            numNeededSnapshots++;

        } else {
            M_ERROR("Camera: %s declining to take snapshot, mode not enabled\n", name);
        }
    }
    /**************************
     *
     *  ¯\_(ツ)_/¯
     *
     */
    else {
        M_ERROR("Camera: %s got unknown Command: %s\n", name, cmd);
    }
}

void PerCameraMgr::EStop(){

    EStopped = true;
    stopped = true;
    pthread_cond_broadcast(&stereoCond);
    pthread_cond_broadcast(&resultCond);

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
        M_ERROR("Find maximum JPEG size from metadata failed.!\n");
        return 0;
    }
    maxJpegBufferSize = jpegBufferMaxSize.data.i32[0];

    float scaleFactor = ((float)width * (float)height) /
        (((float)maxJpegBufferSize - (float)sizeof(camera3_jpeg_blob)) / 3.0f);
    int jpegBufferSize = minJpegBufferSize + (maxJpegBufferSize - minJpegBufferSize) * scaleFactor;

    return jpegBufferSize;
}
