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

#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <mutex>
#include <condition_variable>
#include <modal_start_stop.h>
#include <modal_pipe.h>
#include <voxl_cutils.h>

#include "hal3_camera.h"
#include "common_defs.h"
#include "config_file.h"
#include "debug_log.h"
#include "hal3_camera.h"
#include "rb5_camera_server.h"

#define CONTROL_COMMANDS "set_exp_gain,set_exp,set_gain,start_ae,stop_ae"

// Function prototypes
void   PrintHelpMessage();
int    ErrorCheck(int numInputsScanned, const char* pOptionName);
int    ParseArgs(int         argc,
               char* const pArgv[],
               char*       pConfigFileName,
               DebugLevel* pDebugLevel);

Status StartCamera(CameraType camType);

///<@todo Assumes only one instance of one camera type
PerCameraInfo*          g_pCameraInfo;
bool                    force_enable                            = false;
char                    pipeNames[PIPE_SERVER_MAX_CHANNELS][MODAL_PIPE_MAX_NAME_LEN];

// Attributes for the cleanup thread
pthread_cond_t     quit_cond        = PTHREAD_COND_INITIALIZER;
pthread_mutex_t    quit_mutex;
pthread_t          quit_thread;
volatile bool      stopped_smoothly = false;
const int          quit_estop_delay = 10;   //ESTOP the camera server if it fails to stop normally after 10 seconds
void*  ThreadExitCheck(void *data);



static inline Status setupPipes(){

    Status status = S_OK;
/*
    for (int i = 0; i < g_numCameras && status == S_OK; i++)
    {
        if (g_pCameraInfo[i].isEnabled)
        {

            pipe_info_t info;

            strcpy(info.name       , g_pCameraInfo[i].name);
            strcpy(info.type       , "camera_image_metadata_t");
            strcpy(info.server_name, PROCESS_NAME);

            g_outputChannels[g_pCameraInfo[i].type] = g_maxValidChannel;

            switch (g_pCameraInfo[i].type)
            {
                case CAMTYPE_HIRES:
                    strcat(info.name, "_preview");

                    info.size_bytes = 256*1024*1024;

                    strcpy(info.location, info.name);
                    // 0 means success
                    if (0 == pipe_server_create(g_maxValidChannel,
                                                      info,
                                                      SERVER_FLAG_EN_CONTROL_PIPE))
                    {
                        pipe_server_set_available_control_commands(g_maxValidChannel, CONTROL_COMMANDS);
                        strcpy(pipeNames[g_maxValidChannel], info.name);
                    } else {
                        status = S_ERROR;
                    }
                    g_maxValidChannel++;

                    break;
                    
                case CAMTYPE_TRACKING:
                case CAMTYPE_STEREO:

                    info.size_bytes = 64*1024*1024;

                    strcpy(info.location, info.name);
                    // 0 means success
                    if (0 == pipe_server_create(g_maxValidChannel,
                                                      info,
                                                      SERVER_FLAG_EN_CONTROL_PIPE))
                    {
                        pipe_server_set_available_control_commands(g_maxValidChannel, CONTROL_COMMANDS);
                        strcpy(pipeNames[g_maxValidChannel], info.name);
                    } else {
                        status = S_ERROR;
                    }
                    g_maxValidChannel++;

                    break;

                default:
                    VOXL_LOG_WARNING("------voxl-camera-server WARNING: Bad camera type: %d\n", g_pCameraInfo[i].type);
                    break;
            }
            if(status == S_OK){
                VOXL_LOG_INFO( "Created pipe: %s channel: %d\n", info.name, g_maxValidChannel-1);
            } else {
                VOXL_LOG_ERROR("Failed to create pipe: %s channel: %d\n", info.name, g_maxValidChannel-1);
            }
        }
    }
*/
    return status;
}

// -----------------------------------------------------------------------------------------------------------------------------
// Main camera server function that reads the config file, starts different cameras (using the requested API), sends the
// camera frames on the external interface and also gracefully exits in the event of a shutdown
// -----------------------------------------------------------------------------------------------------------------------------
int main(int argc, char* const argv[])
{

    int           status;
    char          configFileName[FILENAME_MAX] = RB5_CAMERA_SERVER_CONF_FILE;
    DebugLevel    debugLevel = DebugLevel::ERROR; //Default only show errors

    main_running = 1;
    ////////////////////////////////////////////////////////////////////////////////
    // gracefully handle an existing instance of the process and associated PID file
    ////////////////////////////////////////////////////////////////////////////////

    // make sure another instance isn't running
    // if return value is -3 then a background process is running with
    // higher privaledges and we couldn't kill it, in which case we should
    // not continue or there may be hardware conflicts. If it returned -4
    // then there was an invalid argument that needs to be fixed.
    if(kill_existing_process(PROCESS_NAME, 2.0)<-2) return -1;

    // start signal handler so we can exit cleanly
    if(enable_signal_handler()==-1){
        fprintf(stderr,"ERROR: failed to start signal handler\n");
        return -1;
    }

    // make PID file to indicate your project is running
    // due to the check made on the call to rc_kill_existing_process() above
    // we can be fairly confident there is no PID file already and we can
    // make our own safely.
    make_pid_file(PROCESS_NAME);

    status = ParseArgs(argc, argv, &configFileName[0], &debugLevel);

    Debug::SetDebugLevel(debugLevel);

    if (status == S_OK)
    {
        ///<@todo Add support for multiple cameras of the same type - question is how to differentiate one from the other
        //status = ConfigFile::Read(&configFileName[0], &g_numCameras, &g_pCameraInfo);
    }
    else
    {
        PrintHelpMessage();
    }

    if(status == S_OK){
        status = setupPipes();
    }

    if (status == S_OK)
    {


        // @todo we probably have to do this because of the camera starting order requirements
        // Tracking-HiRes-Stereo
        // Tracking-ToF-HiRes
        bool isTracking = false;
        bool isStereo   = false;
        bool isHiRes    = false;
/*
        for (int i = 0; i < g_numCameras; i++)
        {
            if (g_pCameraInfo[i].isEnabled)
            {

                switch (g_pCameraInfo[i].type)
                {
                    case CAMTYPE_TRACKING:
                        if(isTracking){
                            VOXL_LOG_FATAL("Camera Server Does not currently support multiple cameras of the same type\n");
                            VOXL_LOG_FATAL("Exiting\n");
                            i = g_numCameras;
                        } else {
                            isTracking = true;
                        }
                        break;

                    case CAMTYPE_HIRES:
                        if(isHiRes){
                            VOXL_LOG_FATAL("Camera Server Does not currently support multiple cameras of the same type\n");
                            VOXL_LOG_FATAL("Exiting\n");
                            i = g_numCameras;
                        } else {
                            isHiRes = true;
                        }
                        break;

                    case CAMTYPE_STEREO:
                        if(isStereo){
                            VOXL_LOG_FATAL("Camera Server Does not currently support multiple cameras of the same type\n");
                            VOXL_LOG_FATAL("Exiting\n");
                            i = g_numCameras;
                        } else {
                            isStereo = true;
                        }
                        break;

                    default:
                        break;
                }
            }
        }

        if (status == S_OK && isTracking == true)
        {
            status = StartCamera(CAMTYPE_TRACKING);
        }

        if (status == S_OK && isHiRes == true)
        {
            status = StartCamera(CAMTYPE_HIRES);
        }

        if (status == S_OK && isStereo == true)
        {
            status = StartCamera(CAMTYPE_STEREO);
        }*/

        if(status == S_OK){

            VOXL_LOG_FATAL("------ voxl-camera-server: Camera server is now running\n");

            while (main_running)
            {
                usleep(500000);
            }
        }
    }
/*
    //Start timed ESTOP if cameras hang
    pthread_attr_t quitAttr;
    pthread_attr_init(&quitAttr);
    pthread_attr_setdetachstate(&quitAttr, PTHREAD_CREATE_JOINABLE);
    pthread_create(&quit_thread, &quitAttr, ThreadExitCheck, NULL);
    pthread_attr_destroy(&quitAttr);

    VOXL_LOG_FATAL("\n------ voxl-camera-server INFO: Camera server is now stopping\n");
    VOXL_LOG_FATAL("\t\tThere is a chance that it may segfault here, this is a mmqcamera bug, ignore it\n");
    for (int i = 0; i < CAMTYPE_MAX_TYPES; i++)
    {
        if (g_pCamera[i] != NULL)
        {
            VOXL_LOG_FATAL("\n------ voxl-camera-server INFO: Stopping %s camera\n",
                             GetTypeString((CameraType)i));
            //<@todo Need to wrap any hal3 calls behind a API agnostic interface
            // Stop the camera and delete the instance
            g_pCamera[i]->Stop();
            delete g_pCamera[i];
            g_pCamera[i] = NULL;
            VOXL_LOG_FATAL("------ voxl-camera-server INFO: %s camera stopped successfully\n",
                             GetTypeString((CameraType)i));
        }
    }

    stopped_smoothly = true;
    pthread_cond_signal(&quit_cond);

    void *returnval;
    pthread_join(quit_thread, &returnval);

    pipe_server_close_all();

    if (g_pCameraInfo != NULL)
    {
        delete g_pCameraInfo;
        g_pCameraInfo = NULL;
    }

    if(((long) returnval) == 0){
        VOXL_LOG_FATAL("\n------ voxl-camera-server INFO: Camera server exited gracefully\n\n");
    } else {
        VOXL_LOG_FATAL("\n------ voxl-camera-server ERROR: One or more cameras hung on exit, had to ESTOP\n\n");
    }
    return status;*/
}
/*
void*  ThreadExitCheck(void *data){

    struct timespec timeToWait;

    clock_gettime(CLOCK_REALTIME, &timeToWait);

    timeToWait.tv_sec = timeToWait.tv_sec+quit_estop_delay;

    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&mutex);
    pthread_cond_timedwait(&quit_cond, &mutex, &timeToWait);
    pthread_mutex_unlock(&mutex);
    if(stopped_smoothly) return (void *) 0;

    VOXL_LOG_FATAL("Camera Server failed to close cleanly, applying estop\n");

    EStopCameraServer();

    clock_gettime(CLOCK_REALTIME, &timeToWait);

    timeToWait.tv_sec = timeToWait.tv_sec+quit_estop_delay;

    pthread_mutex_lock(&mutex);
    VOXL_LOG_FATAL("Waiting %d more seconds before kill\n", quit_estop_delay);
    pthread_cond_timedwait(&quit_cond, &mutex, &timeToWait);
    pthread_mutex_unlock(&mutex);
    if(stopped_smoothly) return (void *) 1;

    VOXL_LOG_FATAL("Critial Error: Camera Server ESTOP failed, forcing exit\n");

    exit(-1);
}
*/
// -----------------------------------------------------------------------------------------------------------------------------
// Parses the command line arguments to the main function
// -----------------------------------------------------------------------------------------------------------------------------
int ParseArgs(int         argc,                 ///< Number of arguments
              char* const pArgv[],              ///< Argument list
              char*       pConfigFileName,      ///< Returned config file name
              DebugLevel* pDebugLevel)          ///< Returned debug level
{
    static struct option LongOptions[] =
    {
        {"config_file",      required_argument,  0, 'c'},
        {"debug_level",      required_argument,  0, 'd'},
        {"force_enable",     no_argument,        0, 'e'},
        {"help",             no_argument,        0, 'h'},
        {"list-resolutions", no_argument,        0, 'r'},
    };

    int numInputsScanned = 0;
    int optionIndex      = 0;
    int status           = 0;
    int debugLevel       = 0;
    int option;

    while ((status == S_OK) && (option = getopt_long_only (argc, pArgv, ":c:d:ehr", &LongOptions[0], &optionIndex)) != -1)
    {
        switch(option)
        {
            case 'c':
                numInputsScanned = sscanf(optarg, "%s", pConfigFileName);

                if (ErrorCheck(numInputsScanned, LongOptions[optionIndex].name) != 0)
                {
                    printf("No config file specified!\n");
                    status = -EINVAL;
                }

                break;

            case 'd':
                numInputsScanned = sscanf(optarg, "%d", &debugLevel);

                if (ErrorCheck(numInputsScanned, LongOptions[optionIndex].name) != 0)
                {
                    printf("No preview dump frames specified\n");
                    status = -EINVAL;
                }
                else
                {
                    *pDebugLevel = (DebugLevel)debugLevel;

                    if (*pDebugLevel >= DebugLevel::MAX_DEBUG_LEVELS)
                    {
                        VOXL_LOG_FATAL("----- Invalid debug level specified: %d\n", *pDebugLevel);
                        VOXL_LOG_FATAL("----- Max debug level: %d\n", ((int)DebugLevel::MAX_DEBUG_LEVELS - 1));
                        status = S_ERROR;
                        break;
                    }
                }

                break;

            case 'e':
                force_enable = true;
                break;

            case 'h':
                status = -EINVAL; // This will have the effect of printing the help message and exiting the program
                break;
            case 'r':
            	HAL3_print_camera_resolutions();
            	exit(0);

            // Unknown argument
            case '?':
            default:
                printf("Invalid argument passed!\n");
                status = -EINVAL;
                break;
        }
    }

    return status;
}

// -----------------------------------------------------------------------------------------------------------------------------
// Print the help message
// -----------------------------------------------------------------------------------------------------------------------------
void PrintHelpMessage()
{
    printf("\n\nCommand line arguments are as follows:\n");
    printf("\n-c, --config_file  : config file name (No default)");
    printf("\n-d, --debug_level  : debug level (Default 3)");
    printf("\n                 0 : Print all logs");
    printf("\n                 1 : Print info logs");
    printf("\n                 2 : Print warning logs");
    printf("\n                 3 : Print fatal logs");
    //printf("\n-e, --force_enable : Force the camera server to run all cameras");
    //printf("\n                   : even if there are no clients connected");
    printf("\n                   : (will disable non-fatal debug prints)");
    printf("\n-h, --help         : Print this help message");
    printf("\n\nFor example: voxl-camera-server -c /etc/modalai/voxl-camera-server.conf -d 2");
}

// -----------------------------------------------------------------------------------------------------------------------------
// Check for error in parsing the arguments
// -----------------------------------------------------------------------------------------------------------------------------
int ErrorCheck(int numInputsScanned, const char* pOptionName)
{
    int error = 0;
/*
    if (numInputsScanned != 1)
    {
        VOXL_LOG_INFO("ERROR: Invalid argument for %s option\n", pOptionName);
        error = -1;
    }
*/
    return error;
}

// -----------------------------------------------------------------------------------------------------------------------------
// Calling this function will start the passed in camera
// -----------------------------------------------------------------------------------------------------------------------------
Status StartCamera(CameraType camType)
{
    Status status = S_OK;/*
    PerCameraInfo* pCameraInfo = NULL;

    VOXL_LOG_INFO("Starting Camera: %s\n", GetTypeString(camType));

    // If the camera has already been started, simply return
    ///<@todo Does not handle multiple cameras of the same type
    if (g_pCamera[camType] == NULL)
    {
        for (int i = 0; i < g_numCameras; i++)
        {
            if (g_pCameraInfo[i].type == camType)
            {
                pCameraInfo = &g_pCameraInfo[i];
                break;
            }
        }

        if (pCameraInfo != NULL)
        {
            if (pCameraInfo->isEnabled)
            {
                g_pCamera[camType] = new CameraHAL3;

                if (g_pCamera[camType] != NULL)
                {
                    status = g_pCamera[camType]->Start(pCameraInfo);
                }
            }
        }
        else
        {
            VOXL_LOG_ERROR("------ voxl-camera-server ERROR: Invalid camera type given by external interface %d\n", camType);
        }
    }

    if (status == S_OK)
    {
        ConfigFile::PrintCameraInfo(pCameraInfo);
    }
*/
    return status;
}

// -----------------------------------------------------------------------------------------------------------------------------
// Calling this function will perform an emergency stop of the camera server,
// signalling to all camera worker threads that they should stop as soon as possible
// -----------------------------------------------------------------------------------------------------------------------------
void EStopCameraServer()
{/*
    for (int i = 0; i < CAMTYPE_MAX_TYPES; i++)
    {
        if (g_pCamera[i] != NULL)
        {
            g_pCamera[i]->EStop();
        }
    }*/

    main_running = 0;
}
