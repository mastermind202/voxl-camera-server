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
#include "config_file.h"
#include "config_defaults.h"
#include "common_defs.h"
#include "debug_log.h"
#include "voxl_camera_server.h"

#define NUMCAMS  3
#define NUMTYPES 5

// -----------------------------------------------------------------------------------------------------------------------------
// Main camera server configuration tool, recommend that this tool is not called directly, but through
// the voxl-configure-cameras script due to specific supported camera layouts
// -----------------------------------------------------------------------------------------------------------------------------
int main(int argc, char* argv[])
{

    SetDebugLevel(DebugLevel::ALL);


    std::list<PerCameraInfo> cameras;

    PerCameraInfo sfl_info
                    = getDefaultCameraInfo(CAMTYPE_OV7251);
    sfl_info.camId  = 0;
    strcpy(sfl_info.name, "stereo_front_left");

    PerCameraInfo sfr_info
                    = getDefaultCameraInfo(CAMTYPE_OV7251);
    sfr_info.camId  = 1;
    strcpy(sfr_info.name, "stereo_front_right");

    PerCameraInfo tracking_info
                         = getDefaultCameraInfo(CAMTYPE_OV7251);
    tracking_info.camId  = 2;
    strcpy(tracking_info.name, "tracking");

    PerCameraInfo srl_info
                    = getDefaultCameraInfo(CAMTYPE_OV7251);
    srl_info.camId  = 4;
    strcpy(srl_info.name, "stereo_rear_left");

    PerCameraInfo srr_info
                    = getDefaultCameraInfo(CAMTYPE_OV7251);
    srr_info.camId  = 5;
    strcpy(srr_info.name, "stereo_rear_right");

    PerCameraInfo hires_info
                      = getDefaultCameraInfo(CAMTYPE_IMX214);
    hires_info.camId  = 3;
    strcpy(hires_info.name, "hires");


    cameras.push_back(tracking_info);
    cameras.push_back(hires_info);
    cameras.push_back(sfl_info);
    cameras.push_back(sfr_info);
    cameras.push_back(srl_info);
    cameras.push_back(srr_info);

    WriteConfigFile(cameras);

    cameras.erase(cameras.begin(), cameras.end());

    return 0;

}
