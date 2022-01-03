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

#include <cmath>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <modal_json.h>
#include <map>
#include <iostream>
#include <stdint.h>
#include <string>

#include "config_file.h"
#include "config_defaults.h"
#include "debug_log.h"

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
#define JsonWidthString        "width"                    ///< Frame width
#define JsonHeightString       "height"                   ///< Frame height
#define JsonFpsString          "frame_rate"               ///< Fps
#define JsonAEDesiredMSVString "ae_desired_msv"           ///< Modal AE Algorithm Desired MSV
#define JsonAEKPString         "ae_k_p_ns"                ///< Modal AE Algorithm k_p
#define JsonAEKIString         "ae_k_i_ns"                ///< Modal AE Algorithm k_i
#define JsonAEMaxIString       "ae_max_i"                 ///< Modal AE Algorithm max i
#define JsonOverrideIdString   "override_id"              ///< Override id
#define JsonEnabledString      "enabled"                  ///< Is camera enabled

#define DoesNotExistString     "DoesNotExist";


/*
// -----------------------------------------------------------------------------------------------------------------------------
// Gets the camera type from the config file
// -----------------------------------------------------------------------------------------------------------------------------
CameraType ConfigFile::GetCameraType(cJSON* pJsonCameraPort)    ///< Json linked list=
{
    CameraType camType = CAMTYPE_INVALID;

    char typeString[MAX_NAME_LENGTH];

    json_fetch_string_with_default(pJsonCameraPort, JsonTypeString, typeString, MAX_NAME_LENGTH, DoesNotExistString);

    if (strcmp(typeString, DoesNotExistString))
    {
        for (int i = 0; i < CAMTYPE_MAX_TYPES; i++)
        {
            if (!strcmp(typeString, GetTypeString(i)))
            {
                camType = static_cast<CameraType>(i);
                break;
            }
        }
    }
    return camType;
}

// -----------------------------------------------------------------------------------------------------------------------------
// This function retrieves all the fields for a single camera that is specified in the config file
// -----------------------------------------------------------------------------------------------------------------------------
Status ConfigFile::GetCameraInfo(cJSON*          pJsonParent,       ///< Main Json linked list
                                 PerCameraInfo*  pPerCameraInfo)    ///< Returned camera info for each camera
{
    memset(pPerCameraInfo, 0, sizeof(PerCameraInfo));
    cJSON*  pJsonCamPort = NULL;

    pJsonCamPort = json_fetch_object(pJsonParent, pPortName);

    if (pJsonCamPort != NULL)
    {
        pPerCameraInfo->type                  = GetCameraType(pJsonCamPort);
        //Get Name (defaults to the type)
        json_fetch_string_with_default(pJsonCamPort, 
                                       JsonNameString,
                                       pPerCameraInfo->name,
                                       MAX_NAME_LENGTH,
                                       GetTypeString(pPerCameraInfo->type));

        if(!strcmp(pPerCameraInfo->name, getDefaultCameraInfo(CAMTYPE_MAX_TYPES).name))return S_OK;

        pPerCameraInfo->expGainInfo.algorithm = GetCameraAEAlgo(pJsonCamPort);
        for(int i = 0; i < 3; i ++){

            if(!cJSON_HasObjectItem(pJsonCamPort, GetCamModeString(i))){
                pPerCameraInfo->modeInfo[i].format = -1;
                continue;
            } 

            cJSON *pJsonMode = json_fetch_object(pJsonCamPort, GetCamModeString(i));
            char formatString[MAX_NAME_LENGTH];
            json_fetch_string_with_default(pJsonMode,
                                           JsonFormatString,
                                           formatString,
                                           MAX_NAME_LENGTH,
                                           "Invalid");
            pPerCameraInfo->modeInfo[i].format = m_sFmtMap[i][formatString];
            json_fetch_int_with_default(pJsonMode,
                                        JsonWidthString,
                                        &pPerCameraInfo->modeInfo[i].width,
                                        INT_INVALID_VALUE);
            json_fetch_int_with_default(pJsonMode,
                                        JsonHeightString,
                                        &pPerCameraInfo->modeInfo[i].height,
                                        INT_INVALID_VALUE);
            json_fetch_bool_with_default(pJsonMode,
                                         JsonEnabledString,
                                         (int*) &pPerCameraInfo->modeInfo[i].isEnabled,
                                         false);
        }

        ///<@todo Enable the use of overrideId
        json_fetch_int_with_default   (pJsonCamPort, JsonFpsString,          &pPerCameraInfo->fps,                                 30);
        json_fetch_int_with_default   (pJsonCamPort, JsonOverrideIdString,   &pPerCameraInfo->overrideId,                          -1);
        json_fetch_int_with_default   (pJsonCamPort, JsonExposureNsString,   &pPerCameraInfo->expGainInfo.exposureNs,              5259763);
        json_fetch_int_with_default   (pJsonCamPort, JsonGainString,         &pPerCameraInfo->expGainInfo.gain,                    400);

        //ModalAI AE stuff
        json_fetch_int_with_default   (pJsonCamPort, JsonModalAEGainMinString,     (int*)&pPerCameraInfo->expGainInfo.modalai.gain_min,                       0);
        json_fetch_int_with_default   (pJsonCamPort, JsonModalAEGainMaxString,     (int*)&pPerCameraInfo->expGainInfo.modalai.gain_max,                       1000);
        json_fetch_int_with_default   (pJsonCamPort, JsonModalAEGainPerioddString, (int*)&pPerCameraInfo->expGainInfo.modalai.gain_period,                    2);
        json_fetch_int_with_default   (pJsonCamPort, JsonModalAEExpMinString,      (int*)&pPerCameraInfo->expGainInfo.modalai.exposure_min_us,                100);
        json_fetch_int_with_default   (pJsonCamPort, JsonModalAEExpMaxString,      (int*)&pPerCameraInfo->expGainInfo.modalai.exposure_max_us,                33000);
        json_fetch_int_with_default   (pJsonCamPort, JsonModalAEExpPeriodString,   (int*)&pPerCameraInfo->expGainInfo.modalai.exposure_period,                1);
        json_fetch_int_with_default   (pJsonCamPort, JsonModalAEExpOffsetString,   (int*)&pPerCameraInfo->expGainInfo.modalai.exposure_offset_for_gain_calc,  8000);
        json_fetch_int_with_default   (pJsonCamPort, JsonModalAEGoodThreshString,  (int*)&pPerCameraInfo->expGainInfo.modalai.p_good_thresh,                  3);
        json_fetch_float_with_default (pJsonCamPort, JsonModalAEDesiredMSVString ,       &pPerCameraInfo->expGainInfo.modalai.desired_msv,                    58.0);
        json_fetch_float_with_default (pJsonCamPort, JsonModalAEKPString ,               &pPerCameraInfo->expGainInfo.modalai.k_p_ns,                         32000);
        json_fetch_float_with_default (pJsonCamPort, JsonModalAEKIString ,               &pPerCameraInfo->expGainInfo.modalai.k_i_ns,                         20);
        json_fetch_float_with_default (pJsonCamPort, JsonModalAEMaxIString ,             &pPerCameraInfo->expGainInfo.modalai.max_i,                          250);


        json_fetch_bool_with_default(pJsonCamPort, JsonEnabledString,  (int *)(&pPerCameraInfo->isEnabled), false);
        
        if (pPerCameraInfo->type == CAMTYPE_INVALID)
        {
            VOXL_LOG_FATAL("------ voxl-camera-server: FATAL Config file: Missing/bad value for camera type\n");
            return S_ERROR;
        }
        if (pPerCameraInfo->expGainInfo.algorithm == CAMAEALGO_INVALID)
        {
            VOXL_LOG_FATAL("------ voxl-camera-server: FATAL Config file: Missing/bad value for auto_exposure_mode\n");
            return S_ERROR;
        }
        ///<@todo add more error checks
    }
    else
    {
        return S_ERROR;
    }

    VOXL_LOG_INFO("------ voxl-camera-server: Done configuring %s camera\n", pPerCameraInfo->name);

    return S_OK;
}*/

// -----------------------------------------------------------------------------------------------------------------------------
// Read and parse the config file. This function can be modified to support any config file format. The information for each
// camera read from the config file is returned from this function.
//
// Note:
// "ppPerCameraInfo" will point to memory allocated by this function and it is the callers responsibility to free it.
// -----------------------------------------------------------------------------------------------------------------------------
Status ReadConfigFile(list<PerCameraInfo>* cameras)    ///< Returned camera info for each camera in the config file
{

    PerCameraInfo sfl_info
                    = getDefaultCameraInfo(CAMTYPE_OV7251);
    sfl_info.camId  = 0;
    sfl_info.isMono = true;
    sfl_info.width  = 640;
    sfl_info.height = 480;
    strcpy(sfl_info.name, "stereo_front_left");

    PerCameraInfo sfr_info
                    = getDefaultCameraInfo(CAMTYPE_OV7251);
    sfr_info.camId  = 1;
    sfr_info.isMono = true;
    sfr_info.width  = 640;
    sfr_info.height = 480;
    strcpy(sfr_info.name, "stereo_front_right");

    PerCameraInfo tracking_info
                         = getDefaultCameraInfo(CAMTYPE_OV7251);
    tracking_info.camId  = 2;
    tracking_info.isMono = true;
    tracking_info.width  = 640;
    tracking_info.height = 480;
    strcpy(tracking_info.name, "tracking");

    PerCameraInfo srl_info
                    = getDefaultCameraInfo(CAMTYPE_OV7251);
    srl_info.camId  = 4;
    srl_info.isMono = true;
    srl_info.width  = 640;
    srl_info.height = 480;
    strcpy(srl_info.name, "stereo_rear_left");

    PerCameraInfo srr_info
                    = getDefaultCameraInfo(CAMTYPE_OV7251);
    srr_info.camId  = 5;
    srr_info.isMono = true;
    srr_info.width  = 640;
    srr_info.height = 480;
    strcpy(srr_info.name, "stereo_rear_right");

    PerCameraInfo hires_info
                      = getDefaultCameraInfo(CAMTYPE_IMX214);
    hires_info.camId  = 3;
    hires_info.isMono = true;
    hires_info.width  = 640;
    hires_info.height = 480;
    strcpy(hires_info.name, "hires");


    cameras->push_back(tracking_info);
    cameras->push_back(hires_info);
    //cameras->push_back(sfl_info);
    //cameras->push_back(sfr_info);
    //cameras->push_back(srl_info);
    //cameras->push_back(srr_info);


    /*
    cJSON* pJsonParent;

    m_sFmtMap[0]["raw8"]  = FMT_RAW8;
    m_sFmtMap[0]["raw10"] = FMT_RAW10;
    m_sFmtMap[0]["nv12"]  = FMT_NV12;
    m_sFmtMap[0]["nv21"]  = FMT_NV21;

    pJsonParent = json_read_file(pConfigFileName);

    Status status = S_ERROR;

    if (IsConfigFileVersionSupported(pJsonParent) == false)
    {
        VOXL_LOG_FATAL("------ voxl-camera-server: invalid config file\n");
        status = S_ERROR;
    }
    else
    {
        VOXL_LOG_INFO("------ voxl-camera-server INFO: Port J2\n\n");
        status = GetCameraInfo(pJsonParent, JsonPortJ2String, &m_sPerCameraInfo[m_sNumCameras]);

        if (status == S_OK)
        {
            strcpy(&m_sPerCameraInfo[m_sNumCameras].port[0], JsonPortJ2String);
            PrintCameraInfo(&m_sPerCameraInfo[m_sNumCameras]);
            m_sNumCameras++;

        }else return S_ERROR;

        VOXL_LOG_INFO("------ voxl-camera-server INFO: Port J3\n\n");
        status = GetCameraInfo(pJsonParent, JsonPortJ3String, &m_sPerCameraInfo[m_sNumCameras]);

        if (status == S_OK)
        {
            strcpy(&m_sPerCameraInfo[m_sNumCameras].port[0], JsonPortJ3String);
            PrintCameraInfo(&m_sPerCameraInfo[m_sNumCameras]);
            m_sNumCameras++;
        }else return S_ERROR;

        VOXL_LOG_INFO("------ voxl-camera-server INFO: Port J4\n\n");
        status = GetCameraInfo(pJsonParent, JsonPortJ4String, &m_sPerCameraInfo[m_sNumCameras]);

        if (status == S_OK)
        {
            strcpy(&m_sPerCameraInfo[m_sNumCameras].port[0], JsonPortJ4String);
            PrintCameraInfo(&m_sPerCameraInfo[m_sNumCameras]);
            m_sNumCameras++;
        }else return S_ERROR;
    }

    *pNumCameras     = m_sNumCameras;
    *ppPerCameraInfo = new PerCameraInfo[m_sNumCameras];

    memcpy(*ppPerCameraInfo, m_sPerCameraInfo, m_sNumCameras*sizeof(PerCameraInfo));

    if(m_sFileVersion != SupportedVersion){
        VOXL_LOG_INFO("Old config file version detected, writing settings to current version\n");
    }

    Write(pConfigFileName,
        m_sNumCameras,
        *ppPerCameraInfo);
*/
    return S_OK;
}


Status MakeDefaultConfigFile(int config){
	return S_OK;
}

// -----------------------------------------------------------------------------------------------------------------------------
// Read and parse the config file. This function can be modified to support any config file format. The information for each
// camera read from the config file is returned from this function.
//
// -----------------------------------------------------------------------------------------------------------------------------
Status WriteConfigFile(int             pNumCameras,          ///< Number of cameras
                       PerCameraInfo   pPerCameraInfo[])     ///< Camera info for each camera in the config file
{
    /*
    Status returnStatus = S_ERROR;
    cJSON* head = cJSON_CreateObject();

    cJSON_AddNumberToObject(head, JsonVersionString, SupportedVersion);

    for(int i = 0; i < pNumCameras; i++){

        strcpy(pPerCameraInfo[i].port, JsonPortStrings(i));

        PerCameraInfo info = pPerCameraInfo[i];
        cJSON *camera = cJSON_AddObjectToObject(head, pPerCameraInfo[i].port);
        cJSON_AddStringToObject(camera, JsonNameString, info.name);
        cJSON_AddBoolToObject(camera, JsonEnabledString, info.isEnabled);

        if(!info.isEnabled && !strcmp(info.name, getDefaultCameraInfo(CAMTYPE_MAX_TYPES).name)) continue;

        cJSON_AddStringToObject(camera, JsonTypeString, GetTypeString(info.type));
        cJSON_AddNumberToObject(camera, JsonFpsString,  info.fps);
        cJSON_AddNumberToObject(camera, JsonOverrideIdString, info.overrideId);
        cJSON_AddStringToObject(camera, JsonAutoExposureString, GetAEString(info.expGainInfo.algorithm));
        if(info.expGainInfo.exposureNs != -1 && info.expGainInfo.exposureNs != 5259763) 
            cJSON_AddNumberToObject(camera, JsonExposureNsString, info.expGainInfo.exposureNs);
        if(info.expGainInfo.gain != -1 && info.expGainInfo.gain != 400) 
            cJSON_AddNumberToObject(camera, JsonGainString, info.expGainInfo.gain);


        if(info.expGainInfo.algorithm == CAMAEALGO_MODALAI){
            cJSON_AddNumberToObject(camera, JsonModalAEGainMinString,      info.expGainInfo.modalai.gain_min);
            cJSON_AddNumberToObject(camera, JsonModalAEGainMaxString,      info.expGainInfo.modalai.gain_max);
            cJSON_AddNumberToObject(camera, JsonModalAEGainPerioddString,  info.expGainInfo.modalai.gain_period);
            cJSON_AddNumberToObject(camera, JsonModalAEExpMinString,       info.expGainInfo.modalai.exposure_min_us);
            cJSON_AddNumberToObject(camera, JsonModalAEExpMaxString,       info.expGainInfo.modalai.exposure_max_us);
            cJSON_AddNumberToObject(camera, JsonModalAEExpPeriodString,    info.expGainInfo.modalai.exposure_period);
            cJSON_AddNumberToObject(camera, JsonModalAEExpOffsetString,    info.expGainInfo.modalai.exposure_offset_for_gain_calc);
            cJSON_AddNumberToObject(camera, JsonModalAEDesiredMSVString,   info.expGainInfo.modalai.desired_msv);
            cJSON_AddNumberToObject(camera, JsonModalAEKPString,           info.expGainInfo.modalai.k_p_ns);
            cJSON_AddNumberToObject(camera, JsonModalAEKIString,           info.expGainInfo.modalai.k_i_ns);
            cJSON_AddNumberToObject(camera, JsonModalAEMaxIString,         info.expGainInfo.modalai.max_i);
            cJSON_AddNumberToObject(camera, JsonModalAEGoodThreshString,   info.expGainInfo.modalai.p_good_thresh);
            cJSON_AddBoolToObject  (camera, JsonModalAEDisplayDebugString, info.expGainInfo.modalai.display_debug!=0);
        }

        const char *streamNames[3] = {JsonPreviewString, JsonVideoString, JsonSnapshotString};
        for(int j = 0; j < 3; j++){

            if(info.modeInfo[j].format != -1){
                cJSON *stream = cJSON_AddObjectToObject(camera, streamNames[j]);

                cJSON_AddBoolToObject(stream, JsonEnabledString, info.modeInfo[j].isEnabled);
                cJSON_AddNumberToObject(stream, JsonWidthString, info.modeInfo[j].width);
                cJSON_AddNumberToObject(stream, JsonHeightString, info.modeInfo[j].height);
                switch (j){
                    case 0:
                        cJSON_AddStringToObject(stream, JsonFormatString,
                                                GetPreviewFmtString(info.modeInfo[j].format));
                        break;
                    case 1:
                        cJSON_AddStringToObject(stream, JsonFormatString,
                                                GetVideoFmtString(info.modeInfo[j].format));
                        break;
                    case 2:
                        cJSON_AddStringToObject(stream, JsonFormatString,
                                                GetSnapshotFmtString(info.modeInfo[j].format));
                        break;
                }
            }
        }

    }

    FILE *file = fopen(pConfigFileName, "w");
    if(file == NULL){

        VOXL_LOG_FATAL("Error opening config file: %s to write to\n", pConfigFileName);

    }else{
        char *jsonString = cJSON_Print(head);

        //VOXL_LOG_INFO("Writing new configuration to %s:\n%s\n",pConfigFileName, jsonString);
        VOXL_LOG_INFO("Writing new configuration to %s\n",pConfigFileName);
        fwrite(jsonString, 1, strlen(jsonString), file);
        fclose(file);
    }

    cJSON_Delete(head);

    return returnStatus;*/
    return S_OK;
}
