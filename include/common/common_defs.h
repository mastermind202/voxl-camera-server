/*******************************************************************************
 * Copyright 2023 ModalAI Inc.
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
#ifndef COMMON_DEFS_H
#define COMMON_DEFS_H

#include <modal_journal.h>
#include <stdlib.h>
#include <stdio.h>
#include <exposure-hist.h>
#include <exposure-msv.h>
#include "buffer_manager.h"

#define PADDING_DISABLED __attribute__((packed))

static const int INT_INVALID_VALUE   = 0xdeadbeef;
static const int MAXNAMELEN     = 64;
#define MAX_CAMS 7 // todo test this limit

// -----------------------------------------------------------------------------------------------------------------------------
// Supported stream types
// -----------------------------------------------------------------------------------------------------------------------------
typedef enum
{
    // RAW only mode for devices that will simultaneously use more than two cameras.
    // This mode has following limitations: Back end 3A, Face Detect or any additional functionality depending on image/sensor
    // statistics and YUV streams will be disabled

    QCAMERA3_VENDOR_STREAM_CONFIGURATION_RAW_ONLY_MODE = 0x8000,
} QCamera3VendorStreamConfiguration;

//------------------------------------------------------------------------------------------------------------------------------
// Status values that we should use everywhere instead of magic numbers like 0, -1 etc
//------------------------------------------------------------------------------------------------------------------------------
enum Status
{
    S_OUTOFMEM  = -2,
    S_ERROR     = -1,
    S_OK        =  0,
};


//------------------------------------------------------------------------------------------------------------------------------
// List of camera types
//------------------------------------------------------------------------------------------------------------------------------
typedef enum sensor_t
{
    SENSOR_INVALID = -1,   ///< Invalid
    SENSOR_OV7251,
    SENSOR_OV9782,
    SENSOR_IMX214,
    SENSOR_IMX412,
    SENSOR_IMX678,
    SENSOR_TOF,
    SENSOR_MAX_TYPES       ///< Max types
} sensor_t;

#define SENSOR_STRINGS {"ov7251", "ov9782", "imx214", "imx412", "imx678", "pmf-tof"}


// Get the string associated with the type
static const inline char* GetTypeString(int type)
{
    switch ((sensor_t)type){
        case SENSOR_OV7251:      return "ov7251";
        case SENSOR_OV9782:      return "ov9782";
        case SENSOR_IMX214:      return "imx214";
        case SENSOR_IMX412:      return "imx412";
        case SENSOR_IMX678:      return "imx678";
        case SENSOR_TOF:         return "pmd-tof";

        default:               return "Invalid";
    }
}


// Get the type associated with the string
static const inline sensor_t sensor_from_string(char *type)
{

    char lowerType[strlen(type) + 5];
    int i;
    for(i = 0; type[i]; i++){
      lowerType[i] = tolower(type[i]);
    }
    lowerType[i] = 0;

    for(int i = 0; i < SENSOR_MAX_TYPES; i++){
        if(!strcmp(lowerType, GetTypeString((sensor_t)i))){
            return (sensor_t)i;
        }
    }

    return SENSOR_INVALID;
}


// -----------------------------------------------------------------------------------------------------------------------------
// Supported preview formats
// -----------------------------------------------------------------------------------------------------------------------------
enum ImageFormat
{
    FMT_INVALID = -1,
    FMT_RAW8    = 0,    ///< RAW8
    FMT_RAW10,          ///< RAW10
    FMT_NV12,           ///< NV12
    FMT_NV21,           ///< NV21
    FMT_TOF,            ///< TOF (camera manager will translate to proper HAL)
    FMT_MAXTYPES        ///< Max Types
};

#define FORMAT_STRINGS {"raw8", "raw10", "nv12", "nv21", "tof"}


static const inline char* GetImageFmtString(int fmt)
{
    switch ((ImageFormat)fmt){
        case FMT_RAW8:  return "raw8";
        case FMT_RAW10: return "raw10";
        case FMT_NV12:  return "nv12";
        case FMT_NV21:  return "nv21";
        case FMT_TOF:   return "tof";
        default:        return "Invalid";
    }
}


//------------------------------------------------------------------------------------------------------------------------------
// Convert local format type to HAL3 format type
//------------------------------------------------------------------------------------------------------------------------------
static const inline int32_t HalFmtFromType(int fmt)
{
    switch (fmt){
        case FMT_RAW10:
        case FMT_RAW8:
            return HAL_PIXEL_FORMAT_RAW10;

        case FMT_NV21:
        case FMT_NV12:
            return HAL3_FMT_YUV; // defined as HAL_PIXEL_FORMAT_YCbCr_420_888

        case FMT_TOF:

#ifdef APQ8096
            return HAL_PIXEL_FORMAT_BLOB;
#elif QRB5165
            //return HAL_PIXEL_FORMAT_RAW12;
            return HAL_PIXEL_FORMAT_RAW10;
#else
    #error "Platform invalid"
#endif
        default:
            M_ERROR("ERROR: Invalid Preview Format!\n");
            throw -EINVAL;
    }
}


typedef enum AE_MODE
{
    AE_OFF   = 0,
    AE_ISP,
    AE_LME_HIST,
    AE_LME_MSV,
    AE_MAX_MODES
} AE_MODE;

#define AE_STRINGS {"off", "isp", "lme_hist", "lme_msv"}

static const inline char* GetAEModeString(int mode)
{
    switch ((AE_MODE)mode){
        case AE_OFF:      return "off";
        case AE_ISP:      return "isp";
        case AE_LME_HIST: return "lme_hist";
        case AE_LME_MSV:  return "lme_msv";
        default:          return "Invalid";
    }
}

//------------------------------------------------------------------------------------------------------------------------------
// Structure containing information for one camera
// DON'T MESS WITH THE ORDER HERE
// Any changes to this struct should be reflected in camera_defaults.h as well
//------------------------------------------------------------------------------------------------------------------------------
struct PerCameraInfo
{
    char      name[MAXNAMELEN];   ///< Camera name string
    sensor_t  type;               ///< Type of camera
    int       camId;              ///< id of camera
    int       camId2;             ///< id of second camera (if stereo)
    int       isEnabled;          ///< Is the camera enabled/disabled
    int       fps;                ///< Frame rate - number of frames per second

    int     en_preview;
    int     pre_width;            ///< Preview Width of the frame
    int     pre_height;           ///< Preview Height of the frame
    int     pre_format;           ///< Preview Frame format

    int     en_small_video;
    int     small_video_width;    ///< Video Stream Width of the frame
    int     small_video_height;   ///< Video Stream Height of the frame
    int     small_video_bitrate;  ///< Video Stream Bitrate
    int     small_video_h265_en;  ///< Video Stream h265 encoding enabled

    int     en_large_video;
    int     large_video_width;    ///< Video Record Width of the frame
    int     large_video_height;   ///< Video Record Height of the frame
    int     large_video_bitrate;  ///< Video Record Bitrate
    int     large_video_h265_en;  ///< Video Record h265 encoding enabled

    int     en_snapshot;
    int     snap_width;            ///< Snapshot Width of the frame
    int     snap_height;           ///< Snapshot Height of the frame

    int     ind_exp;               ///< For stereo pairs, run exposure independently?

    AE_MODE ae_mode;
    modal_exposure_config_t      ae_hist_info; ///< ModalAI AE data (Histogram)
    modal_exposure_msv_config_t  ae_msv_info;  ///< ModalAI AE data (MSV)

    int     standby_enabled;       ///< Standby enabled for lidar
    int     decimator;             ///< Decimator to use for standby
};





#endif // COMMON_DEFS
