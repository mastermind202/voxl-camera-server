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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <string>
#include <list>
#include <algorithm>
#include <modal_json.h>

#include "config_file.h"
#include "config_defaults.h"
#include "debug_log.h"

#define CURRENT_VERSION 0.1
#define CONFIG_FILE_NAME "/etc/modalai/voxl-camera-server.conf"

using namespace std;

// Parse the JSON entries from the linked list represented by pJsonParent
static Status GetCameraInfo(cJSON*          pJsonParent,       ///< Main Json linked list
                            PerCameraInfo*  pPerCameraInfo);   ///< Camera info from the config file
static bool         IsConfigFileVersionSupported(cJSON* pJsonParent);
static CameraType   GetCameraType(cJSON* pCameraInfo);

// These are the main element strings in the JSON camera config file - so to speak these are variable names in the config
// file. Each of these variables have values associated with them and the values could be integer, int64, strings etc
#define JsonVersionString      "version"                  ///< Config file version
#define JsonTypeString         "type"                     ///< Camera type
#define JsonNameString         "name"                     ///< Camera name
#define JsonFPSString          "fps"                      ///< Camera fps
#define JsonFlipString         "flip"                     ///< Camera flip?
//#define JsonWidthString        "width"                    ///< Frame width
//#define JsonHeightString       "height"                   ///< Frame height
//#define JsonFpsString          "frame_rate"               ///< Fps
#define JsonAEDesiredMSVString "ae_desired_msv"           ///< Modal AE Algorithm Desired MSV
#define JsonAEKPString         "ae_k_p_ns"                ///< Modal AE Algorithm k_p
#define JsonAEKIString         "ae_k_i_ns"                ///< Modal AE Algorithm k_i
#define JsonAEMaxIString       "ae_max_i"                 ///< Modal AE Algorithm max i
#define JsonCameraIdString     "camera_id"                ///< Camera id
#define JsonCameraId2String    "camera_id_second"         ///< Camera id 2
#define JsonEnabledString      "enabled"                  ///< Is camera enabled

#define contains(a, b) (std::find(a.begin(), a.end(), b) != a.end())

// -----------------------------------------------------------------------------------------------------------------------------
// Read and parse the config file. This function can be modified to support any config file format. The information for each
// camera read from the config file is returned from this function.
//
// Note:
// "cameras" will contain memory allocated by this function and it is the callers responsibility to free/pop it it.
// -----------------------------------------------------------------------------------------------------------------------------
Status ReadConfigFile(list<PerCameraInfo> &cameras)    ///< Returned camera info for each camera in the config file
{

    cJSON* head = json_read_file(CONFIG_FILE_NAME);

    std::list<int> cameraIds;
    std::list<string> cameraNames;

    int numCameras;
    for(cJSON *cur = json_fetch_array(head, "cameras", &numCameras)->child;
        cur != NULL;
        cur = cur->next){

        CameraType type;
        char buffer[64];
        if(json_fetch_string(cur, JsonTypeString, buffer, 63)){
            VOXL_LOG_ERROR("Error Reading config file: camera type not specified for: %s\n", buffer);
            goto ERROR_EXIT;
        }

        if((type=GetCameraTypeFromString(buffer)) == CAMTYPE_INVALID){
            VOXL_LOG_ERROR("Error Reading config file: invalid type: %s\n", buffer);
            goto ERROR_EXIT;
        }

        PerCameraInfo info = getDefaultCameraInfo(type);


        if(json_fetch_string(cur, JsonNameString, info.name, 63)){
            VOXL_LOG_ERROR("Error Reading config file: camera name not specified\n", info.name);
            goto ERROR_EXIT;
        }

        if(contains(cameraNames, info.name)){
            VOXL_LOG_ERROR("Error Reading config file: multiple cameras with name: %s\n", info.name);
            goto ERROR_EXIT;
        }

        if(json_fetch_int(cur, JsonCameraIdString, &(info.camId))){
            VOXL_LOG_ERROR("Error Reading config file: camera id not specified for: %s\n", info.name);
            goto ERROR_EXIT;
        }else if(contains(cameraIds, info.camId)){
            VOXL_LOG_ERROR("Error Reading config file: multiple cameras with id: %d\n", info.camId);
            goto ERROR_EXIT;
        } else {
            cameraIds.push_back(info.camId);
        }

        if(!cJSON_HasObjectItem(cur, JsonCameraId2String) || json_fetch_int(cur, JsonCameraId2String, &(info.camId2))){
            //VOXL_LOG_ALL("No secondary id found for camera: %s, assuming mono\n", info.name);
            info.camId2 = -1;
        } else if(contains(cameraIds, info.camId2)){
            VOXL_LOG_ERROR("Error Reading config file: multiple cameras with id: %d\n", info.camId);
            goto ERROR_EXIT;
        } else {
            //VOXL_LOG_ALL("Secondary id found for camera: %s, assuming stereo\n", info.name);
            cameraIds.push_back(info.camId2);
        }

        int tmp;
        json_fetch_bool_with_default(cur, JsonEnabledString, &tmp, true);
        info.isEnabled = tmp;
        json_fetch_bool_with_default(cur, JsonFlipString,    &tmp, false);
        info.flip = tmp;

        json_fetch_float_with_default (cur, JsonAEDesiredMSVString ,   &info.expGainInfo.desired_msv, 58.0);
        json_fetch_float_with_default (cur, JsonAEKPString ,           &info.expGainInfo.k_p_ns,      32000);
        json_fetch_float_with_default (cur, JsonAEKIString ,           &info.expGainInfo.k_i_ns,      20);
        json_fetch_float_with_default (cur, JsonAEMaxIString ,         &info.expGainInfo.max_i,       250);


        cameraNames.push_back(info.name);
        cameras.push_back(info);

    }

    cJSON_free(head);
    return S_OK;

    ERROR_EXIT:

    cJSON_free(head);
    cameras.erase(cameras.begin(), cameras.end());
    return S_ERROR;

}

// -----------------------------------------------------------------------------------------------------------------------------
// Read and parse the config file. This function can be modified to support any config file format. The information for each
// camera read from the config file is returned from this function.
//
// -----------------------------------------------------------------------------------------------------------------------------
void WriteConfigFile(list<PerCameraInfo> cameras)     ///< Camera info for each camera in the config file
{
    cJSON* head = cJSON_CreateObject();

    cJSON_AddNumberToObject(head, JsonVersionString, CURRENT_VERSION);

    cJSON* camArray = cJSON_AddArrayToObject(head, "cameras");

    for(PerCameraInfo info : cameras){

        cJSON* node = cJSON_CreateObject();

        cJSON_AddStringToObject(node, JsonNameString, info.name);
        cJSON_AddBoolToObject  (node, JsonEnabledString, info.isEnabled);
        cJSON_AddBoolToObject  (node, JsonFlipString, info.flip);

        cJSON_AddStringToObject(node, JsonTypeString, GetTypeString(info.type));
        cJSON_AddNumberToObject(node, JsonCameraIdString, info.camId);

        if(info.camId2 != -1) cJSON_AddNumberToObject(node, JsonCameraId2String, info.camId2);

        cJSON_AddNumberToObject (node, JsonAEDesiredMSVString ,  info.expGainInfo.desired_msv);
        cJSON_AddNumberToObject (node, JsonAEKPString ,          info.expGainInfo.k_p_ns);
        cJSON_AddNumberToObject (node, JsonAEKIString ,          info.expGainInfo.k_i_ns);
        cJSON_AddNumberToObject (node, JsonAEMaxIString ,        info.expGainInfo.max_i);

        cJSON_AddItemToArray(camArray, node);

    }

    FILE *file = fopen(CONFIG_FILE_NAME, "w");
    if(file == NULL){

        VOXL_LOG_FATAL("Error opening config file: %s to write to\n", CONFIG_FILE_NAME);

    }else{
        char *jsonString = cJSON_Print(head);

        //VOXL_LOG_INFO("Writing new configuration to %s:\n%s\n",pConfigFileName, jsonString);
        VOXL_LOG_INFO("Writing new configuration to %s\n",CONFIG_FILE_NAME);
        fwrite(jsonString, 1, strlen(jsonString), file);

        fclose(file);
        free(jsonString);
    }

    cJSON_Delete(head);
}
