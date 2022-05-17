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

#include "buffer_manager.h"
#include "common_defs.h"
#include "exposure-hist.h"

#define NUM_MODULE_OPEN_ATTEMPTS 10

//HAL3 will lag the framerate if we attempt autoexposure any more frequently than this
#define NUM_SKIPPED_FRAMES 4

using namespace std;

// Forward Declaration
class BufferManager;
class PerCameraMgr;

//------------------------------------------------------------------------------------------------------------------------------
// Everything needed to handle a single camera
//------------------------------------------------------------------------------------------------------------------------------
class PerCameraMgr
{
public:
    PerCameraMgr(PerCameraInfo perCameraInfo);
    ~PerCameraMgr();

    // Start the camera so that it starts streaming frames
    void Start();
    // Stop the camera and stop sending any more requests to the camera module
    void Stop();
    void EStop();

    int getNumClients(){
        return pipe_server_get_num_clients(outputChannel);
    }

    const PerCameraInfo        configInfo;                     ///< Per camera config information
    const uint8_t              outputChannel;
    const int32_t              cameraId;                       ///< Camera id
    char                       name[MAX_NAME_LENGTH];
    const bool                 en_video;
    const bool                 en_snapshot;
    const int32_t              p_width;                        ///< Preview Width
    const int32_t              p_height;                       ///< Preview Height
    const int32_t              p_halFmt;                       ///< Preview HAL format
    const int32_t              v_width;                        ///< Preview Width
    const int32_t              v_height;                       ///< Preview Height
    const int32_t              v_halFmt;                       ///< Preview HAL format
    const int32_t              s_width;                        ///< Preview Width
    const int32_t              s_height;                       ///< Preview Height
    const int32_t              s_halFmt;                       ///< Preview HAL format

private:

    void* ThreadPostProcessResult();
    void* ThreadIssueCaptureRequests();

    // Call the camera module and pass it the stream configuration
    int  ConfigureStreams();
    // Initialize the MPA pipes
    int  SetupPipes();
    void HandleControlCmd(char* cmd);
    void addClient();

    // Call the camera module to get the default camera settings
    int ConstructDefaultRequestSettings();
    // Send one capture request to the camera module
    int  ProcessOneCaptureRequest(int frameNumber);

    void ProcessPreviewFrame (BufferBlock* bufferBlockInfo);
    void ProcessSnapshotFrame(BufferBlock* bufferBlockInfo);

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
        STREAM_VIDEO,
        STREAM_SNAPSHOT,
        STREAM_INVALID
    };

    STREAM_ID GetStreamId(camera3_stream_t *stream){
        if (stream == &p_stream) {
            return STREAM_PREVIEW;
        } else if (stream == &v_stream) {
            return STREAM_VIDEO;
        } else if (stream == &s_stream) {
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
                return &p_bufferGroup;
            case STREAM_VIDEO:
                return &v_bufferGroup;
            case STREAM_SNAPSHOT:
                return &s_bufferGroup;
            default:
                return NULL;
        }
    }

    camera_module_t*                  pCameraModule;               ///< Camera module
    ModalExposureHist                 expInterface;
    Camera3Callbacks                  cameraCallbacks;             ///< Camera callbacks
    camera3_device_t*                 pDevice;                     ///< HAL3 device
    camera3_stream_t                  p_stream;                    ///< Stream to be used for the preview request
    camera3_stream_t                  v_stream;                    ///< Stream to be used for the preview request
    camera3_stream_t                  s_stream;                    ///< Stream to be used for the preview request
    android::CameraMetadata           requestMetadata;             ///< Per request metadata
    BufferGroup                       p_bufferGroup;               ///< Buffer manager per stream
    BufferGroup                       v_bufferGroup;               ///< Buffer manager per stream
    BufferGroup                       s_bufferGroup;               ///< Buffer manager per stream
    pthread_t                         requestThread;               ///< Request thread private data
    pthread_t                         resultThread;                ///< Result Thread private data
    pthread_mutex_t                   requestMutex;                ///< Mutex for list access
    pthread_cond_t                    requestCond;                 ///< Condition variable for wake up
    pthread_mutex_t                   resultMutex;                 ///< Mutex for list access
    pthread_cond_t                    resultCond;                  ///< Condition variable for wake up
    bool                              usingAE;                     ///< Internal Auto Exposure (False for ISP)
    bool                              is10bit;                     ///< Marks if a raw preview image is raw10 or raw8
    int64_t                           currentFrameNumber = 0;      ///< Frame Number
    int64_t                           currentTimestamp;            ///< Timestamp
    int64_t                           currentExposure;             ///< Exposure
    int32_t                           currentGain;                 ///< Gain
    int64_t                           setExposure;                 ///< Exposure
    int32_t                           setGain;                     ///< Gain
    std::list<camera3_stream_buffer>  resultMsgQueue;
    pthread_mutex_t                   stereoMutex;                 ///< Mutex for stereo comms
    pthread_cond_t                    stereoCond;                  ///< Condition variable for wake up
    PerCameraMgr*                     otherMgr;                    ///< Pointer to the partner manager in a stereo pair
    PCM_MODE                          partnerMode;                 ///< Mode for mono/stereo
    uint8_t*                          childFrame = NULL;           ///< Pointer to the child frame, guarded with stereoMutex
    camera_image_metadata_t*          childInfo  = NULL;           ///< Pointer to the child frame info
    bool                              stopped = false;             ///< Indication for the thread to terminate
    bool                              EStopped = false;            ///< Emergency Stop, terminate without any cleanup
    int                               lastResultFrameNumber = -1;  ///< Last frame the capture result thread should wait for before terminating
    pthread_mutex_t                   snapshotMutex;               ///< Mutex for list access
    pthread_cond_t                    snapshotCond;                ///< Condition variable for wake up
    std::list<char *>                 snapshotQueue;

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
 * @brief      Checks to see if
 *
 * @param[in]  camId  The camera to print resolutions for
 * 					-1 prints all available cameras
 */
bool HAL3_is_config_supported(int camId, int width, int height, int format);


#endif
