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
#include "debug_log.h"
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

        CameraType type;
        int camId, camId2;

        if (name_str == NULL) { //this trigger shouldn't be possible
            printf("Error: missing name for camera %d\n", i-1);
            cameras.clear();
            return -1;
        }

        if (type_str == NULL) {
            printf("Error: missing type for camera %d\n", i-1);
            cameras.clear();
            return -1;
        } else if ((type = GetCameraTypeFromString(type_str)) == CAMTYPE_INVALID) {
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

        if (cam2_str == NULL) { //we're ok if they don't provide 2
            camId2=-1;
        } else {
            camId2=atoi(cam2_str);
        }

        PerCameraInfo info = getDefaultCameraInfo(type);
        info.camId  = camId;
        info.camId2 = camId2;
        strcpy(info.name, name_str);

        cameras.push_back(info);

    }

    WriteConfigFile(cameras);

    cameras.clear();

    return 0;

}
