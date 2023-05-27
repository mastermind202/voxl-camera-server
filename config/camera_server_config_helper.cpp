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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <getopt.h>
#include <list>
#include "config_file.h"
#include "config_defaults.h"
#include "common_defs.h"
#include <modal_journal.h>
#include "voxl_camera_server.h"

// -----------------------------------------------------------------------------------------------------------------------------
// Main camera server configuration tool, recommend that this tool is not called directly, but through
// the voxl-configure-cameras script due to specific supported camera layouts
// -----------------------------------------------------------------------------------------------------------------------------
int main(int argc, char* argv[])
{

    std::list<PerCameraInfo> cameras;

    for (int i = 1; i < argc; i++){

        char *name_str = strtok(argv[i], ":");
        char *type_str = strtok(NULL, ":");
        char *cam1_str = strtok(NULL, ":");
        char *cam2_str = strtok(NULL, ":");
        char *opt_str = strtok(NULL, ":");

        sensor_t type;
        int camId;

        if (name_str == NULL) { //this trigger shouldn't be possible
            printf("Error: missing name for camera %d\n", i-1);
            cameras.clear();
            return -1;
        }

        if (type_str == NULL) {
            printf("Error: missing type for camera %d\n", i-1);
            cameras.clear();
            return -1;
        } else if ((type = sensor_from_string(type_str)) == SENSOR_INVALID) {
            printf("Error: invalid type: %s for camera %d\n", type_str, i-1);
            cameras.clear();
            return -1;
        }

        if (cam1_str == NULL) {
            printf("Error: missing camera ID for camera %d\n", i-1);
            cameras.clear();
            return -1;
        } else {
            camId=atoi(cam1_str);
        }



        // copy in the basics
        PerCameraInfo info = getDefaultCameraInfo(type);
        info.camId  = camId;
        info.camId2 = -1;
        info.isEnabled = true;
        strcpy(info.name, name_str);


        // optional fields. Allow "N" in the last field to inidcate disabling the camera
        // TODO formalize that more, just for debug and development for now
        if(cam2_str != NULL){
            if(cam2_str[0]=='N') info.isEnabled = false;
            else{
                info.camId2=atoi(cam2_str);
                if(opt_str!=NULL && opt_str[0]=='N') info.isEnabled = false;
            }
        }

        cameras.push_back(info);

    }

    ReadConfigFile(cameras);
    cameras.clear();

    return 0;

}
