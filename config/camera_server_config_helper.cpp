/*******************************************************************************
 * Copyright 2021 ModalAI Inc.
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

#include <stdio.h>
#include <string.h>
#include <getopt.h>
#include <list>
#include "camera_config.h"
#include "camera_defaults.h"
#include "common_defs.h"
#include "debug_log.h"
#include "voxl_camera_server.h"

#define NUMCAMS  3
#define NUMTYPES 5
static const char* valid_cams[NUMTYPES] = {"none", "hires", "tof", "tracking", "stereo"};

static PerCameraInfo *cams;

static bool factory_mode = false;

static void _print_valid_types(){

    printf("Valid types are:\n");

    for(int i = 0; i < NUMTYPES; i++){
        printf("\t%s\n", valid_cams[i]);
    }
}

static void _print_usage(void) {
    printf("\n\
Please don't call me, this script does not safety check valid camera combinations\n\
Please call voxl-configure-cameras instead\n\
\n\
This will configure the camera server in factory mode\n\
with specified cameras in the specified slots.\n\
\n\
Options are:\n\
-f                 factory reset, use defaults for the specified cameras (required)\n\
-h                 print this help message\n\n\
\n\
Call format: \n\
camera-server-config-helper -f (J2) (J3) (J4)\n\
Sample calls: \n\
camera-server-config-helper -f hires stereo tracking\n\
camera-server-config-helper -f none none tracking\n\
camera-server-config-helper -f none tof none\n\
\n\
Please don't call me, this script does not safety check valid camera combinations\n\
Please call voxl-configure-cameras instead\n");
    return;
}

static bool _validate_type(char *type){

    return GetCameraTypeFromString(type) != CAMTYPE_INVALID || !strcmp(type, valid_cams[0]);
}

static int _parse_opts(int argc, char* argv[])
{
    static struct option long_options[] =
    {
        {"factory",            no_argument,        0, 'f'},
        {"help",               no_argument,        0, 'h'},
        {0, 0, 0, 0}
    };

    while(1){
        int option_index = 0;
        int c = getopt_long(argc, argv, "fh", long_options, &option_index);

        if(c == -1) break; // Detect the end of the options.

        switch(c){
        case 0:
            // for long args without short equivalent that just set a flag
            // nothing left to do so just break.
            if (long_options[option_index].flag != 0) break;
            break;
            break;

        case 'f':
            factory_mode = true;

            for(int i = 2; i < 5; i++){
                if(!_validate_type(argv[i])){
                    printf("Invalid camera type in port: J%d: %s\n",i, argv[i]);
                    _print_valid_types();
                    return -1;
                }
            }
            return 0;

        case 'h':
            _print_usage();
            return -1;

        default:
            _print_usage();
            return -1;
        }
    }

    return -1;
}

// -----------------------------------------------------------------------------------------------------------------------------
// Main camera server configuration tool, recommend that this tool is not called directly, but through
// the voxl-configure-cameras script due to specific supported camera layouts
// -----------------------------------------------------------------------------------------------------------------------------
int main(int argc, char* argv[])
{

    Debug::SetDebugLevel(DebugLevel::ALL);
    if(_parse_opts(argc, argv)) return -1;

    cams = new PerCameraInfo[NUMCAMS];

    int hiresc = 0, stereoc = 0, trackc = 0;
    for(int i = 0; i < NUMCAMS; i++){

        //Get default camera data from args 2,3,and 4 in factory mode
        cams[i] = getDefaultCameraInfo(GetCameraTypeFromString(argv[i+2]));

    }

    ConfigFile::Write(VOXL_CAMERA_SERVER_CONF_FILE,
                      NUMCAMS,
                      cams);

    if(cams != NULL) delete cams;

    return 0;

}
