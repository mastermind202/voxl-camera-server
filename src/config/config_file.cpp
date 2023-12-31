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
#include <modal_journal.h>

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
#define JsonPWidthString       "preview_width"            ///< Preview Frame width
#define JsonPHeightString      "preview_height"           ///< Preview Frame height
#define JsonEWidthString       "encode_width"             ///< Encode Frame width
#define JsonEHeightString      "encode_height"            ///< Encode Frame height
#define JsonSWidthString       "snapshot_width"           ///< Snapshot Frame width
#define JsonSHeightString      "snapshot_height"          ///< Snapshot Frame height
#define JsonFpsString          "frame_rate"               ///< Fps
#define JsonIndExpString       "independent_exposure"     ///< Independent exposure for a stereo pair
#define JsonAEDesiredMSVString "ae_desired_msv"           ///< Modal AE Algorithm Desired MSV
#define JsonAEFilterAlpha      "ae_filter_alpha"          ///< Modal AE MSV Algo filter alpha
#define JsonAEIgnoreFraction   "ae_ignore_fraction"       ///< Modal AE MSV algo ignore frac for most saturated
#define JsonAESlope            "ae_slope"                 ///< Modal AE MSV algo Exp/Gain Slope
#define JsonAEExposurePeriod   "ae_exposure_period"       ///< Modal AE MSV algo Exposure Period
#define JsonAEGainPeriod       "ae_gain_period"           ///< Modal AE MSV algo Gain Period
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
            M_ERROR("Reading config file: camera type not specified for: %s\n", buffer);
            goto ERROR_EXIT;
        }

        if((type=GetCameraTypeFromString(buffer)) == CAMTYPE_INVALID){
            M_ERROR("Reading config file: invalid type: %s\n", buffer);
            goto ERROR_EXIT;
        }

        PerCameraInfo info = getDefaultCameraInfo(type);

        if(json_fetch_string(cur, JsonNameString, info.name, 63)){
            M_ERROR("Reading config file: camera name not specified\n", info.name);
            goto ERROR_EXIT;
        }

        if(contains(cameraNames, info.name)){
            M_ERROR("Reading config file: multiple cameras with name: %s\n", info.name);
            goto ERROR_EXIT;
        }

        if(json_fetch_int(cur, JsonCameraIdString, &(info.camId))){
            M_ERROR("Reading config file: camera id not specified for: %s\n", info.name);
            goto ERROR_EXIT;
        }else if(contains(cameraIds, info.camId)){
            M_ERROR("Reading config file: multiple cameras with id: %d\n", info.camId);
            goto ERROR_EXIT;
        } else {
            cameraIds.push_back(info.camId);
        }

        if(!cJSON_HasObjectItem(cur, JsonCameraId2String) || json_fetch_int(cur, JsonCameraId2String, &(info.camId2))){
            M_VERBOSE("No secondary id found for camera: %s, assuming mono\n", info.name);
            info.camId2 = -1;
        } else if(contains(cameraIds, info.camId2)){
            M_ERROR("Reading config file: multiple cameras with id: %d\n", info.camId);
            goto ERROR_EXIT;
        } else {
            M_VERBOSE("Secondary id found for camera: %s, assuming stereo\n", info.name);
            cameraIds.push_back(info.camId2);
        }

        json_fetch_int_with_default (cur, JsonFpsString ,   &info.fps, info.fps);

        int tmp;
        json_fetch_bool_with_default(cur, JsonEnabledString, &tmp, true);
        info.isEnabled = tmp;
        json_fetch_bool_with_default(cur, JsonFlipString,    &tmp, false);
        info.flip = tmp;
        json_fetch_bool_with_default(cur, JsonIndExpString,  &tmp, false);
        info.ind_exp = tmp;

        json_fetch_int_with_default  (cur, JsonPWidthString,        &info.p_width,   info.p_width);
        json_fetch_int_with_default  (cur, JsonPHeightString,       &info.p_height,  info.p_height);
        json_fetch_int_with_default  (cur, JsonEWidthString,        &info.e_width,   info.e_width);
        json_fetch_int_with_default  (cur, JsonEHeightString,       &info.e_height,  info.e_height);
        json_fetch_int_with_default  (cur, JsonSWidthString,        &info.s_width,   info.s_width);
        json_fetch_int_with_default  (cur, JsonSHeightString,       &info.s_height,  info.s_height);

        json_fetch_float_with_default (cur, JsonAEDesiredMSVString , &info.ae_hist_info.desired_msv, info.ae_hist_info.desired_msv);
        json_fetch_float_with_default (cur, JsonAEKPString ,         &info.ae_hist_info.k_p_ns,      info.ae_hist_info.k_p_ns);
        json_fetch_float_with_default (cur, JsonAEKIString ,         &info.ae_hist_info.k_i_ns,      info.ae_hist_info.k_i_ns);
        json_fetch_float_with_default (cur, JsonAEMaxIString ,       &info.ae_hist_info.max_i,       info.ae_hist_info.max_i);

        json_fetch_float_with_default (cur, JsonAEDesiredMSVString , &info.ae_msv_info.desired_msv,                       info.ae_msv_info.desired_msv);
        json_fetch_float_with_default (cur, JsonAEFilterAlpha ,      &info.ae_msv_info.msv_filter_alpha,                  info.ae_msv_info.msv_filter_alpha);
        json_fetch_float_with_default (cur, JsonAEIgnoreFraction ,   &info.ae_msv_info.max_saturated_pix_ignore_fraction, info.ae_msv_info.max_saturated_pix_ignore_fraction);
        json_fetch_float_with_default (cur, JsonAESlope ,            &info.ae_msv_info.exposure_gain_slope,               info.ae_msv_info.exposure_gain_slope);
        json_fetch_int_with_default   (cur, JsonAEExposurePeriod ,   (int*)&info.ae_msv_info.exposure_update_period, (int)info.ae_msv_info.exposure_update_period);
        json_fetch_int_with_default   (cur, JsonAEGainPeriod ,       (int*)&info.ae_msv_info.gain_update_period,     (int)info.ae_msv_info.gain_update_period);

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

        cJSON_AddStringToObject(node, JsonNameString,     info.name);
        cJSON_AddBoolToObject  (node, JsonEnabledString,  info.isEnabled);
        cJSON_AddNumberToObject(node, JsonFpsString,      info.fps);
        cJSON_AddStringToObject(node, JsonTypeString,     GetTypeString(info.type));
        cJSON_AddNumberToObject(node, JsonCameraIdString, info.camId);

        if(info.camId2 != -1) cJSON_AddNumberToObject(node, JsonCameraId2String, info.camId2);

        cJSON_AddNumberToObject  (node, JsonPWidthString,        info.p_width);
        cJSON_AddNumberToObject  (node, JsonPHeightString,       info.p_height);

        if (info.en_encode) {
            cJSON_AddNumberToObject  (node, JsonEWidthString,        info.e_width);
            cJSON_AddNumberToObject  (node, JsonEHeightString,       info.e_height);
        }

        if (info.en_snapshot) {
            cJSON_AddNumberToObject  (node, JsonSWidthString,        info.s_width);
            cJSON_AddNumberToObject  (node, JsonSHeightString,       info.s_height);
        }

        if(info.camId2 != -1) cJSON_AddBoolToObject(node, JsonIndExpString, info.ind_exp);

        if(info.ae_mode == AE_LME_HIST){
            cJSON_AddNumberToObject (node, JsonAEDesiredMSVString ,  info.ae_hist_info.desired_msv);
            cJSON_AddNumberToObject (node, JsonAEKPString ,          info.ae_hist_info.k_p_ns);
            cJSON_AddNumberToObject (node, JsonAEKIString ,          info.ae_hist_info.k_i_ns);
            cJSON_AddNumberToObject (node, JsonAEMaxIString ,        info.ae_hist_info.max_i);
        } else if(info.ae_mode == AE_LME_MSV){
            cJSON_AddNumberToObject (node, JsonAEDesiredMSVString ,  info.ae_msv_info.desired_msv);
            cJSON_AddNumberToObject (node, JsonAEFilterAlpha ,       info.ae_msv_info.msv_filter_alpha);
            cJSON_AddNumberToObject (node, JsonAEIgnoreFraction ,    info.ae_msv_info.max_saturated_pix_ignore_fraction);
            cJSON_AddNumberToObject (node, JsonAESlope ,             info.ae_msv_info.exposure_gain_slope);
            cJSON_AddNumberToObject (node, JsonAEExposurePeriod ,    info.ae_msv_info.exposure_update_period);
            cJSON_AddNumberToObject (node, JsonAEGainPeriod ,        info.ae_msv_info.gain_update_period);

        }

        cJSON_AddItemToArray(camArray, node);

    }

    FILE *file = fopen(CONFIG_FILE_NAME, "w");
    if(file == NULL){

        M_ERROR("Opening config file: %s to write to\n", CONFIG_FILE_NAME);

    }else{
        char *jsonString = cJSON_Print(head);

        //M_DEBUG("Writing new configuration to %s:\n%s\n",pConfigFileName, jsonString);
        M_DEBUG("Writing new configuration to %s\n",CONFIG_FILE_NAME);
        fwrite(jsonString, 1, strlen(jsonString), file);

        fclose(file);
        free(jsonString);
    }

    cJSON_Delete(head);
}
