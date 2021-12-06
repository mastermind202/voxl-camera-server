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

#include "config_defaults.h"

// Function prototypes
void   PrintHelpMessage();
int    ParseArgs(int         argc,
                 char* const argv[]);

Status StartCamera(PerCameraInfo cam);

static int            g_numCameras;
static PerCameraInfo* g_pCameraInfo;

// -----------------------------------------------------------------------------------------------------------------------------
// Main camera server function that reads the config file, starts different cameras (using the requested API), sends the
// camera frames on the external interface and also gracefully exits in the event of a shutdown
// -----------------------------------------------------------------------------------------------------------------------------
int main(int argc, char* const argv[])
{

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

    if(int retval = ParseArgs(argc, argv)){
    	if(retval > 0) {
    		return 0;
    	}
        PrintHelpMessage();
    	return -1;
    }

    main_running = 1;

    if(ReadConfigFile(&g_numCameras, &g_pCameraInfo)){
    	VOXL_LOG_FATAL("ERROR: Failed to read config file\n");
    	return -1;
    }

    g_numCameras = 1;
    g_pCameraInfo = new PerCameraInfo[1];
    g_pCameraInfo[0] = getDefaultCameraInfo(CAMTYPE_OV7251);
    g_pCameraInfo[0].camId = 2;
    g_pCameraInfo[0].isMono = true;
    strcpy(g_pCameraInfo[0].name, "tracking");

    PerCameraMgr* mgr = new PerCameraMgr(g_pCameraInfo[0]);
    mgr->Start();

    VOXL_LOG_FATAL("------ voxl-camera-server: Camera server is now running\n");

    while (main_running)
    {
        usleep(500000);
    }

    mgr->Stop();

/*
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

	//Hal3 managers should deal with this but safer to call here anyways
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

// -----------------------------------------------------------------------------------------------------------------------------
// Parses the command line arguments to the main function
//
// retvals:
// 		-1 : error
// 		 0 : ready to run camera server
// 		 1 : no error, but completed a subtask and do not need to run camera server
//
// -----------------------------------------------------------------------------------------------------------------------------
int ParseArgs(int         argc,                 ///< Number of arguments
              char* const argv[])               ///< Argument list
{

    static struct option LongOptions[] =
    {
    	{"configure",        required_argument,  0, 'c'},
        {"debug-level",      required_argument,  0, 'd'},
        {"help",             no_argument,        0, 'h'},
        {"list-resolutions", no_argument,        0, 'r'},
    };

    int optionIndex      = 0;
    int option;

    while ((option = getopt_long (argc, argv, ":c:d:hr", &LongOptions[0], &optionIndex)) != -1)
    {
        switch(option)
        {

            case 'c':

            	int config;
                if (sscanf(optarg, "%d", &config) != 1)
                {
                    printf("ERROR: failed to parse config number specified after -c flag\n");
                    return -1;
                }

                if(MakeDefaultConfigFile(config)){
                	return -1;
                }

                return 1;

            case 'd':

            	int debugLevel;
                if (sscanf(optarg, "%d", &debugLevel) != 1)
                {
                    printf("ERROR: failed to parse debug level specified after -d flag\n");
                    return -1;
                }

                if (debugLevel >= DebugLevel::MAX_DEBUG_LEVELS || debugLevel < DebugLevel::ALL)
                {
                    VOXL_LOG_FATAL("ERROR: Invalid debug level specified: %d\n", debugLevel);
                    VOXL_LOG_FATAL("-----  Max debug level: %d\n", ((int)DebugLevel::MAX_DEBUG_LEVELS - 1));
                    VOXL_LOG_FATAL("-----  Min debug level: %d\n", ((int)DebugLevel::ALL));
                    return -1;
                }

                SetDebugLevel((DebugLevel)debugLevel);

                break;

            case 'h':
            	PrintHelpMessage();
                return 1;

            case 'r':
            	// -1 Tells the module to print all cameras
            	HAL3_print_camera_resolutions(-1);
            	return 1;

            case ':':
            	printf("ERROR: Missing argument for %s\n", argv[optopt]);
				return -1;
            // Unknown argument
            case '?':
                printf("ERROR: Invalid argument passed: %s\n", argv[optopt]);
                return -1;
        }
    }

    return 0;
}

// -----------------------------------------------------------------------------------------------------------------------------
// Print the help message
// -----------------------------------------------------------------------------------------------------------------------------
void PrintHelpMessage()
{
    printf("\n\nCommand line arguments are as follows:\n");
    printf("\n-d, --debug-level       : debug level (Default 2)");
    printf("\n                      0 : Print all logs");
    printf("\n                      1 : Print info logs");
    printf("\n                      2 : Print warning logs");
    printf("\n                      3 : Print fatal logs");
    printf("\n                        : (will disable non-fatal debug prints)");
    printf("\n-h, --help              : Print this help message");
    printf("\n-r, --list-resolutions  : List the available cameras and their resolutions");
    printf("\n\n");
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
