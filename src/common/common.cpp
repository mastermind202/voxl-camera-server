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

#include <string.h>
#include <ctype.h>
#include <modal_pipe_interfaces.h>

#include "common_defs.h"

// -----------------------------------------------------------------------------------------------------------------------------
// Supported preview formats
// -----------------------------------------------------------------------------------------------------------------------------
const char* GetImageFmtString(int fmt){
    switch ((ImageFormat)fmt){
        case FMT_RAW8:  return "raw8";
        case FMT_RAW10: return "raw10";
        case FMT_NV12:  return "nv12";
        case FMT_NV21:  return "nv21";
        default:        return "Invalid";
    }
}

//------------------------------------------------------------------------------------------------------------------------------
// List of camera types
//------------------------------------------------------------------------------------------------------------------------------
// Get the string associated with the type
const char* GetTypeString(int type)
{
    switch ((CameraType)type){
        case CAMTYPE_OV7251:      return "ov7251";
        case CAMTYPE_IMX214:      return "imx214";

    #ifdef APQ8096
        case CAMTYPE_OV7251_PAIR: return "ov7251-pair";
        case CAMTYPE_TOF:         return "pmd-tof";
    #endif
        
        default:               return "Invalid";
    }
}

// Get the type associated with the string
const CameraType GetCameraTypeFromString(char *type)
{

    char lowerType[strlen(type) + 5];
    int i;
    for(i = 0; type[i]; i++){
      lowerType[i] = tolower(type[i]);
    }
    lowerType[i] = 0;

    for(int i = 0; i < CAMTYPE_MAX_TYPES; i++){
        if(!strcmp(lowerType, GetTypeString((CameraType)i))){
            return (CameraType)i;
        }
    }

    return CAMTYPE_INVALID;
}

// -----------------------------------------------------------------------------------------------------------------------------
// Prints The info for a specific camera
// -----------------------------------------------------------------------------------------------------------------------------
void PrintCameraInfo(PerCameraInfo pCameraInfo)    ///< Camera info
{

    printf("\t Name       : %s\n", pCameraInfo.name);
    printf("\t Enabled    : %d\n", pCameraInfo.isEnabled);

    if(pCameraInfo.isMono){

    	printf("\t Type       : mono %s\n", GetTypeString(pCameraInfo.type));
    	printf("\t Cam ID     : %d\n", pCameraInfo.camId);

    } else {

    	printf("\t Type       : stereo %s\n", GetTypeString(pCameraInfo.type));
    	printf("\t Cam ID 1   : %d\n", pCameraInfo.camId);
    	printf("\t Cam ID 2   : %d\n", pCameraInfo.camId2);

    }

    printf("\t Preview Width      : %d\n", pCameraInfo.p_width);
    printf("\t Preview Height     : %d\n", pCameraInfo.p_height);
    printf("\t Preview Format     : %s\n", GetImageFmtString(pCameraInfo.p_format));
    if(pCameraInfo.en_video){
        printf("\t Video Record Width      : %d\n", pCameraInfo.v_width);
        printf("\t Video Record Height     : %d\n", pCameraInfo.v_height);
    }
    if(pCameraInfo.en_snapshot){
        printf("\t Snapshot Width      : %d\n", pCameraInfo.s_width);
        printf("\t Snapshot Height     : %d\n", pCameraInfo.s_height);
    }
    printf("\t FPS        : %d\n", pCameraInfo.fps);

    modal_exposure_print_config(pCameraInfo.expGainInfo);

    printf("\n");
}
