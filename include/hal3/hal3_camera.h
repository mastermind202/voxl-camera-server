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
#define MODULE_ATTEMPT_DELAY_MS  1000

//HAL3 will lag the framerate if we attempt autoexposure any more frequently than this
#define NUM_SKIPPED_FRAMES 4


using namespace std;

// Forward Declaration
class BufferManager;
class PerCameraMgr;


// -----------------------------------------------------------------------------------------------------------------------------
// Thread Data for camera request and result thread
// -----------------------------------------------------------------------------------------------------------------------------
typedef struct ThreadData
{
    pthread_t         thread;                   ///< Thread handle
    pthread_mutex_t   mutex;                    ///< Mutex for list access
    pthread_cond_t    cond;                     ///< Condition variable for wake up
    std::list<void*>  msgQueue;                 ///< Message queue
    PerCameraMgr*     pCameraMgr;               ///< Pointer to the per camera mgr class
    void*             pPrivate;                 ///< Any private information if need be
    volatile bool     stop;                     ///< Indication for the thread to terminate
    volatile bool     EStop;                    ///< Emergency Stop, terminate without waiting
    volatile int      lastResultFrameNumber;    ///< Last frame the capture result thread should wait for before terminating
} ThreadData;

//------------------------------------------------------------------------------------------------------------------------------
// Everything needed to handle a single camera
//------------------------------------------------------------------------------------------------------------------------------
class PerCameraMgr
{
public:
    PerCameraMgr(PerCameraInfo perCameraInfo);
    ~PerCameraMgr() {}

    CameraType GetCameraType() const
    {
        return m_cameraConfigInfo.type;
    }
    // Get preview buffer manager
    BufferGroup* GetBufferManager()
    {
        return m_pBufferManager;
    }
    // Get preview width
    int32_t GetWidth() const
    {
        return m_cameraConfigInfo.width;
    }
    // Get preview height
    int32_t GetHeight() const
    {
        return m_cameraConfigInfo.height;
    }
    // Get preview format
    int32_t GetFormat() const
    {
        return m_cameraConfigInfo.format;
    }
    // Get frame rate
    int32_t GetFrameRate() const
    {
        return m_cameraConfigInfo.fps;
    }
    bool IsStopped(){
        return 
            m_requestThread.stop ||
            m_resultThread.stop;
    }
    void EStop();
    const char *GetName()
    {
        return m_cameraConfigInfo.name;
    }

    bool IsUsingAE()
    {
        return m_usingAE;
    }
    void SetUsingAE(bool use)
    {
        m_usingAE = use;
    }
    int32_t getCameraID(){
        return m_cameraId;
    }

    // Start the camera so that it starts streaming frames
    void Start();
    // Stop the camera and stop sending any more requests to the camera module
    void Stop();
    // Send one capture request to the camera module
    int  ProcessOneCaptureRequest(int frameNumber);
    // Process one capture result sent by the camera module
    void ProcessOneCaptureResult(const camera3_capture_result* pHalResult);
    // The request thread calls this function to indicate it sent the last request to the camera module and will not send any
    // more requests
    void StoppedSendingRequest(int framenumber);
    void StopResultThread();

    void addClient();

    int getNumClients(){
        return pipe_server_get_num_clients(m_outputChannel);
    }
    uint8_t                   m_outputChannel;

    // Camera module calls this function to pass on the capture frame result
    static void  CameraModuleCaptureResult(const camera3_callback_ops_t *cb,const camera3_capture_result *hal_result);
    // Camera module calls this function to notify us of any messages
    static void  CameraModuleNotify(const camera3_callback_ops_t *cb, const camera3_notify_msg_t *msg);
    // Call the camera module and pass it the stream configuration
    int          ConfigureStreams();
    // Initialize the MPA pipes
    int          SetupPipes();
    // Call the camera module to get the default camera settings
    virtual void ConstructDefaultRequestSettings();

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

    // Per frame value saved off from the metadata. It is indexed using framenumber.
    struct MetadataInfo
    {
        int64_t timestampNsecs; ///< Frame timestamp
        int64_t exposureNsecs;  ///< Frame exposure values indexed with frame number
        int     gain;           ///< Frame gain value
    };

    bool                      m_isMono;                         ///< Is mono
    int32_t                   m_cameraId;                       ///< Camera id
    camera_info               m_pHalCameraInfo;                 ///< Camera info
    camera3_device_t*         m_pDevice;                        ///< HAL3 device
    android::CameraMetadata   m_requestMetadata;                ///< Per request metadata
    BufferGroup*              m_pBufferManager;                 ///< Buffer manager per stream
    camera3_stream_t*         m_stream;                         ///< Stream to be used for the camera request
    camera_module_t*          m_pCameraModule;                  ///< Camera module
    int32_t                   m_halFmt;                         ///< HAL format to use for preview
    Camera3Callbacks          m_cameraCallbacks;                ///< Camera callbacks
    ThreadData                m_requestThread;                  ///< Request thread private data
    ThreadData                m_resultThread;                   ///< Result Thread private data
    std::mutex                m_expgainCondMutex;               ///< Mutex to be used with the condition variable
    PerCameraInfo             m_cameraConfigInfo;               ///< Per camera config information
    bool                      m_usingAE = true;                 ///< Disabled AE via control pipe
    int64_t                   m_currentTimestamp;               ///< Timestamp
    int64_t                   m_currentExposure;                ///< Exposure
    int32_t                   m_currentGain;                    ///< Gain
    int64_t                   m_setExposure;                    ///< Exposure
    int32_t                   m_setGain;                        ///< Gain
    ModalExposureHist         m_expInterface;
};

//------------------------------------------------------------------------------------------------------------------------------
// Main interface
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
 * @brief      Prints the resolutions of camera(s)
 *
 * @param[in]  camId  The camera to print resolutions for
 * 					-1 prints all available cameras
 */
bool HAL3_is_config_supported(int camId, int width, int height, int format);


#endif
