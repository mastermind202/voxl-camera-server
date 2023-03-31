/*******************************************************************************
 * Copyright 2022 ModalAI Inc.
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

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <list>
#include <modal_start_stop.h>
#include <modal_pipe.h>

#include "hal3_camera.h"
#include "common_defs.h"
#include "config_file.h"
#include <modal_journal.h>
#include "voxl_camera_server.h"

// Function prototypes
static void   PrintHelpMessage();
static int    ParseArgs(int         argc,
                 char* const argv[]);

static list<PerCameraMgr*> mgrs;
static void cleanManagers();

static int source_is_config_file = 1;

// -----------------------------------------------------------------------------------------------------------------------------
// Main camera server function that reads the config file, starts different cameras (using the requested API), sends the
// camera frames on the external interface and also gracefully exits in the event of a shutdown
// -----------------------------------------------------------------------------------------------------------------------------
int main(int argc, char* const argv[])
{

    ////////////////////////////////////////////////////////////////////////////////
    // gracefully handle an existing instance of the process and associated PID file
    ////////////////////////////////////////////////////////////////////////////////

    if(int retval = ParseArgs(argc, argv)){
        if(retval > 0) { //No error, but not supposed to run (i.e. just print options)
            return 0;
        }
        PrintHelpMessage();
        return -1;
    }

    // make sure another instance isn't running
    // if return value is -3 then a background process is running with
    // higher privileges, and we couldn't kill it, in which case we should
    // not continue or there may be hardware conflicts. If it returned -4
    // then there was an invalid argument that needs to be fixed.
    if(kill_existing_process(PROCESS_NAME, 2.0)<-2) return -1;

    // start signal handler so we can exit cleanly
    if(enable_signal_handler()==-1){
        M_ERROR("Failed to start signal handler\n");
        return -1;
    }

    // make PID file to indicate your project is running
    // due to the check made on the call to rc_kill_existing_process() above
    // we can be fairly confident there is no PID file already and we can
    // make our own safely.
    make_pid_file(PROCESS_NAME);

    pipe_set_process_priority(THREAD_PRIORITY_RT_MED);

    main_running = 1;

    list<PerCameraInfo> cameraInfo;

    if(source_is_config_file){
        if(ReadConfigFile(cameraInfo)){
            M_ERROR("Failed to read config file\n");
            return -1;
        }
    } else { //We're gonna ask hal3 for a list of cameras
        if(HAL3_get_debug_configuration(cameraInfo)){
            M_ERROR("Failed to get valid debug configuration\n");
            return -1;
        }
    }

    M_DEBUG("------ voxl-camera-server: Starting camera server\n");

    for(PerCameraInfo info : cameraInfo){

        if(!info.isEnabled) {
            M_VERBOSE("\tSkipping Camera: %s, configuration marked disabled\n", info.name);
            continue;
        }
        M_DEBUG("Starting Camera: %s\n", info.name);

        try{
            PerCameraMgr *mgr = new PerCameraMgr(info);
            mgr->Start();
            mgrs.push_back(mgr);
        } catch(int) {
            M_ERROR("Failed to start camera: %s, exiting\n", info.name);
            cleanManagers();
            return -1;
        }

        M_DEBUG("Started Camera: %s\n", info.name);

    }

    M_PRINT("\n------ voxl-camera-server: Camera server is now running\n");

    while (main_running)
    {
        usleep(500000);
    }

    M_PRINT("\n------ voxl-camera-server: Camera server is now stopping\n");
    cleanManagers();

    M_PRINT("\n------ voxl-camera-server: Camera server exited gracefully\n\n");

    return 0;
}

static void cleanManagers(){
    for(PerCameraMgr *mgr : mgrs){
        mgr->Stop();
        delete mgr;
        M_DEBUG("\tStopped Camera: %s\n",
                         mgr->name);
    }

    mgrs.erase(mgrs.begin(), mgrs.end());
    pipe_server_close_all();
}

// -----------------------------------------------------------------------------------------------------------------------------
// Parses the command line arguments to the main function
// -----------------------------------------------------------------------------------------------------------------------------
static int ParseArgs(int         argc,                 ///< Number of arguments
                     char* const argv[])               ///< Argument list
{

    static struct option LongOptions[] =
    {
        {"debug-level",      required_argument,  0, 'd'},
        {"help",             no_argument,        0, 'h'},
        {"list",             no_argument,        0, 'l'},
        {"self-identify",    no_argument,        0, 's'},
    };

    int optionIndex = 0;
    int option;

    while ((option = getopt_long (argc, argv, ":d:hls", &LongOptions[0], &optionIndex)) != -1)
    {
        switch(option)
        {
            case 'd':
                int debugLevel;
                if (sscanf(optarg, "%d", &debugLevel) != 1)
                {
                    M_ERROR("Failed to parse debug level specified after -d flag\n");
                    return -1;
                }

                if (debugLevel >= PRINT || debugLevel < VERBOSE)
                {
                    M_ERROR("Invalid debug level specified: %d\n", debugLevel);
                    return -1;
                }

                M_JournalSetLevel((M_JournalLevel) debugLevel);
                break;

            case 'l':
                M_JournalSetLevel(DEBUG);
                HAL3_print_camera_resolutions(-1);
                exit(0);

            case 's':
                source_is_config_file = 0;
                break;

            case 'h':
                return -1;

            case ':':
            	M_ERROR("Missing argument for %s\n", argv[optopt]);
				return -1;
            // Unknown argument
            case '?':
                M_ERROR("Invalid argument passed: %s\n", argv[optopt]);
                return -1;
        }
    }

    return 0;
}

// -----------------------------------------------------------------------------------------------------------------------------
// Print the help message
// -----------------------------------------------------------------------------------------------------------------------------
static void PrintHelpMessage()
{
    M_PRINT("\nCommand line arguments are as follows:\n\n");
    M_PRINT("-d, --debug-level       : Log debug level (Default 2)\n");
    M_PRINT("                      0 : Print verbose logs\n");
    M_PRINT("                      1 : Print >= info logs\n");
    M_PRINT("                      2 : Print >= warning logs\n");
    M_PRINT("                      3 : Print only fatal logs\n");
    M_PRINT("-h, --help              : Print this help message\n");
    M_PRINT("-l, --list              : Shows a list of plugged in cameras and some info about them\n");
    M_PRINT("-s, --self-identify     : Debug mode where camera server attempts to self-identify cameras\n");
    M_PRINT("                              instead of pulling information from config file\n\n");
}

// -----------------------------------------------------------------------------------------------------------------------------
// Calling this function will perform an emergency stop of the camera server,
// signalling to all camera worker threads that they should stop as soon as possible
// -----------------------------------------------------------------------------------------------------------------------------
void EStopCameraServer()
{
    for (PerCameraMgr *mgr : mgrs)
    {
        mgr->EStop();
    }

    main_running = 0;
}
