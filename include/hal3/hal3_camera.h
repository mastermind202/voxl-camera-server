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

#ifndef VOXL_HAL3_CAMERA
#define VOXL_HAL3_CAMERA

#include <camera/CameraMetadata.h>
#include <hardware/camera3.h>
#include <list>
#include <string>
#include <modal_pipe.h>
#include <mutex>
#include <condition_variable>
#include <atomic>

#include "ringbuffer.h"
#include "buffer_manager.h"
#include "common_defs.h"
#include "exposure-hist.h"
#include "exposure-msv.h"
#include "omx_video_encoder.h"
#include "tof_interface.hpp"

#define NUM_MODULE_OPEN_ATTEMPTS 10

//HAL3 will lag the framerate if we attempt autoexposure any more frequently than this
#define NUM_SKIPPED_FRAMES 4

using namespace std;

#ifdef APQ8096
using namespace modalai;
#endif

// Forward Declaration
class BufferManager;
class PerCameraMgr;

//------------------------------------------------------------------------------------------------------------------------------
// Everything needed to handle a single camera
//------------------------------------------------------------------------------------------------------------------------------
class PerCameraMgr : public IRoyaleDataListener
{
public:
    PerCameraMgr() ;
    PerCameraMgr(PerCameraInfo perCameraInfo);
    ~PerCameraMgr();

    // Callback function for the TOF bridge to provide the post processed TOF data
    bool RoyaleDataDone(const void*             pData,
                        uint32_t                size,
                        int64_t                 timestamp,
                        RoyaleListenerType      dataType);

    // Start the camera so that it starts streaming frames
    void Start();
    // Stop the camera and stop sending any more requests to the camera module
    void Stop();
    void EStop();

    int getNumClients(){
        if( partnerMode != MODE_STEREO_SLAVE ) {
            return pipe_server_get_num_clients(outputChannel);
        } else {
            return pipe_server_get_num_clients(otherMgr->outputChannel);
        }
    }

    const PerCameraInfo        configInfo;                     ///< Per camera config information
    const uint8_t              outputChannel;
    const int32_t              cameraId;                       ///< Camera id
          char                 name[MAX_NAME_LENGTH];
    const bool                 en_stream;
    const bool                 en_record;
    const bool                 en_snapshot;
    const int32_t              pre_width;                        ///< Preview Width
    const int32_t              pre_height;                       ///< Preview Height
    const int32_t              pre_halfmt;                       ///< Preview HAL format
    const int32_t              str_width;                        ///< Stream Width
    const int32_t              str_height;                       ///< Stream Height
    const int32_t              str_halfmt;                       ///< Stream HAL format
    const int32_t              str_bitrate;                      ///< Stream Bitrate
    const int32_t              rec_width;                        ///< Record Width
    const int32_t              rec_height;                       ///< Record Height
    const int32_t              rec_halfmt;                       ///< Record HAL format
    const int32_t              rec_bitrate;                      ///< Record Bitrate
    const int32_t              snap_width;                        ///< Snapshot Width
    const int32_t              snap_height;                       ///< Snapshot Height
    const int32_t              snap_halfmt;                       ///< Snapshot HAL format
          AE_MODE              ae_mode;


private:

    void* ThreadPostProcessResult();
    void* ThreadIssueCaptureRequests();

    // Call the camera module and pass it the stream configuration
    int  ConfigureStreams();
    // Initialize the MPA pipes
    int  SetupPipes();
    void HandleControlCmd(char* cmd);

    // Call the camera module to get the default camera settings
    int ConstructDefaultRequestSettings();

    // called by ProcessOneCaptureRequest to decide if we need to send requests
    // for the preview stream, snapshot, record, and stream streams handled separately
    int HasClientForPreviewFrame();

    // Send one capture request to the camera module
    int  ProcessOneCaptureRequest(int frameNumber);

    typedef std::pair<int, camera3_stream_buffer> image_result;

    void ProcessPreviewFrame (image_result result);
    void ProcessStreamFrame  (image_result result);
    void ProcessRecordFrame  (image_result result);
    void ProcessSnapshotFrame(image_result result);

    int getMeta(int frameNumber, camera_image_metadata_t *retMeta){
        for(camera_image_metadata_t c : resultMetaRing){
            // fprintf(stderr, "%s, %d : %d - %d\n", __FUNCTION__, __LINE__, frameNumber, c.frame_id );

            if(c.frame_id == frameNumber){
                *retMeta = c;
                return 0;
            }
        }
        return -1;
    }

    // camera3_callback_ops is returned to us in every result callback. We piggy back any private information we may need at
    // the time of processing the frame result. When we register the callbacks with the camera module, we register the starting
    // address of this structure (which is camera3_callbacks_ops) but followed by our private information. When we receive a
    // pointer to this structure at the time of capture result, we typecast the incoming pointer to this structure type pointer
    // and access our private information
    struct Camera3Callbacks
    {
        camera3_callback_ops cameraCallbacks;
        void* pPrivate;
    };

    void ProcessOneCaptureResult(const camera3_capture_result* pHalResult);
    static void CameraModuleCaptureResult(const camera3_callback_ops_t *cb, const camera3_capture_result* pHalResult);
    static void CameraModuleNotify(const camera3_callback_ops_t *cb, const camera3_notify_msg_t *msg);

    enum PCM_MODE {
        MODE_MONO,
        MODE_STEREO_MASTER,
        MODE_STEREO_SLAVE
    };

    enum STREAM_ID {
        STREAM_PREVIEW,
        STREAM_STREAM,
        STREAM_RECORD,
        STREAM_SNAPSHOT,
        STREAM_INVALID
    };

    STREAM_ID GetStreamId(camera3_stream_t *stream){
        if (stream == &pre_stream) {
            return STREAM_PREVIEW;
        } else if (stream == &str_stream) {
            return STREAM_STREAM;
        } else if (stream == &rec_stream) {
            return STREAM_RECORD;
        } else if (stream == &snap_stream) {
            return STREAM_SNAPSHOT;
        } else {
            return STREAM_INVALID;
        }
    }

    BufferGroup *GetBufferGroup(camera3_stream_t *stream){
        return GetBufferGroup(GetStreamId(stream));
    }
    BufferGroup *GetBufferGroup(STREAM_ID stream){
        switch (stream){
            case STREAM_PREVIEW:
                return &pre_bufferGroup;
            case STREAM_STREAM:
                return &str_bufferGroup;
            case STREAM_RECORD:
                return &rec_bufferGroup;
            case STREAM_SNAPSHOT:
                return &snap_bufferGroup;
            default:
                return NULL;
        }
    }

    camera_module_t*                    pCameraModule;               ///< Camera module
    VideoEncoder*                       pVideoEncoderStream;
    VideoEncoder*                       pVideoEncoderRecord;
    ModalExposureHist                   expHistInterface;
    ModalExposureMSV                    expMSVInterface;
    Camera3Callbacks                    cameraCallbacks;             ///< Camera callbacks
    camera3_device_t*                   pDevice;                     ///< HAL3 device
    uint8_t                             num_streams;
    camera3_stream_t                    pre_stream;                  ///< Stream to be used for the preview request
    camera3_stream_t                    str_stream;                  ///< Stream to be used for the stream request
    camera3_stream_t                    rec_stream;                  ///< Stream to be used for the record request
    camera3_stream_t                    snap_stream;                 ///< Stream to be used for the snapshots request
    android::CameraMetadata             requestMetadata;             ///< Per request metadata
    BufferGroup                         pre_bufferGroup;             ///< Buffer manager per stream
    BufferGroup                         str_bufferGroup;             ///< Buffer manager per stream
    BufferGroup                         rec_bufferGroup;             ///< Buffer manager per stream
    BufferGroup                         snap_bufferGroup;            ///< Buffer manager per stream
    pthread_t                           requestThread;               ///< Request thread private data
    pthread_t                           resultThread;                ///< Result Thread private data
    pthread_mutex_t                     resultMutex;                 ///< Mutex for list access
    pthread_cond_t                      resultCond;                  ///< Condition variable for wake up
    pthread_mutex_t                     aeMutex;                     ///< Mutex for list access
    bool                                is10bit;                     ///< Marks if a raw preview image is raw10 or raw8
    int64_t                             setExposure = 5259763;       ///< Exposure
    int32_t                             setGain     = 800;           ///< Gain
    list<image_result>                  resultMsgQueue;
    RingBuffer<camera_image_metadata_t> resultMetaRing;
    pthread_mutex_t                     stereoMutex;                 ///< Mutex for stereo comms
    pthread_cond_t                      stereoCond;                  ///< Condition variable for wake up
    PerCameraMgr*                       otherMgr;                    ///< Pointer to the partner manager in a stereo pair
    PCM_MODE                            partnerMode;                 ///< Mode for mono/stereo
    uint8_t*                            childFrame = NULL;           ///< Pointer to the child frame, guarded with stereoMutex
    uint8_t*                            childFrame_uvHead = NULL;
    camera_image_metadata_t             childInfo;                   ///< Copy of the child frame info
    bool                                stopped = false;             ///< Indication for the thread to terminate
    bool                                EStopped = false;            ///< Emergency Stop, terminate without any cleanup
    int                                 lastResultFrameNumber = -1;  ///< Last frame the capture result thread should wait for before terminating
    list<char *>                        snapshotQueue;
    atomic_int                          numNeededSnapshots {0};
    int                                 lastSnapshotNumber = 0;
    int                                 streamOutputChannel = -1;
    int                                 recordOutputChannel = -1;

    ///< TOF Specific members

    // APQ and qrb have different royale APIs, maybe someday we'll backport the
    //     clean api to voxl1 but right now it's baked into the system image
    #ifdef APQ8096
        void*                          tof_interface;                ///< TOF interface to process the TOF camera raw data
    #elif QRB5165
        TOFInterface*                  tof_interface;                ///< TOF interface to process the TOF camera raw data
    #endif

    uint32_t                           TOFFrameNumber = 0;
    uint8_t                            IROutputChannel;
    uint8_t                            DepthOutputChannel;
    uint8_t                            ConfOutputChannel;
    uint8_t                            PCOutputChannel;
    uint8_t                            FullOutputChannel;
    int                                tofFrameCounter = 0;

    void setMaster(PerCameraMgr *master) { ///< Tells a camera manager that the passed in pointer is it's master
        partnerMode = MODE_STEREO_SLAVE;
        otherMgr    = master;
    }

};

//------------------------------------------------------------------------------------------------------------------------------
// Main HAL interface
//------------------------------------------------------------------------------------------------------------------------------
camera_module_t* HAL3_get_camera_module();

/**
 * @brief      Prints the resolutions of camera(s)
 *
 * @param[in]  camId  The camera to print resolutions for
 * 					-1 prints all available cameras
 */
void HAL3_print_camera_resolutions(int camId);

/**
 * @brief      Checks to see if a given config is supported for a camera
 *
 * @param[in]  camId  The camera to print resolutions for
 * 					-1 prints all available cameras
 */
bool HAL3_is_config_supported(int camId, int width, int height, int format);

/**
 * @brief      Generates a list of cameras to run based off what's plugged in
 *
 * @param[in]  cameras  Reference to list that we'll populate with camera info
 */
Status HAL3_get_debug_configuration(std::list<PerCameraInfo>& cameras);

#endif
