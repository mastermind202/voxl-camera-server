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
static const int MAX_NAME_LENGTH     = 64;

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

//------------------------------------------------------------------------------------------------------------------------------
// List of camera types
//------------------------------------------------------------------------------------------------------------------------------
enum CameraType
{
    CAMTYPE_INVALID = -1,   ///< Invalid
    CAMTYPE_OV7251,
    CAMTYPE_OV9782,
    CAMTYPE_IMX214,
    CAMTYPE_TOF,
    CAMTYPE_MAX_TYPES       ///< Max types
};

enum AE_MODE
{
    AE_OFF   = 0,
    AE_ISP,
    AE_LME_HIST,
    AE_LME_MSV
};

//------------------------------------------------------------------------------------------------------------------------------
// Structure containing information for one camera
// Any changes to this struct should be reflected in camera_defaults.h as well
//------------------------------------------------------------------------------------------------------------------------------
struct PerCameraInfo
{
    char          name[MAX_NAME_LENGTH]; ///< Camera name string
    CameraType    type;                  ///< Type of camera
    bool          isMono;                ///< mono or stereo
    int           camId;                 ///< id of camera
    int           camId2;                ///< id of second camera (if stereo)
    bool          isEnabled;             ///< Is the camera enabled/disabled

    int     fps;                ///< Frame rate - number of frames per second
    int     p_width;            ///< Preview Width of the frame
    int     p_height;           ///< Preview Height of the frame
    int     p_format;           ///< Preview Frame format
    bool    en_record;
    int     r_width;            ///< Video Record Width of the frame
    int     r_height;           ///< Video Record Height of the frame
    bool    en_snapshot;
    int     s_width;            ///< Snapshot Width of the frame
    int     s_height;           ///< Snapshot Height of the frame
    bool    flip;               ///< Flip?
    bool    ind_exp;            ///< For stereo pairs, run exposure independently?

    AE_MODE ae_mode;
    modal_exposure_config_t      ae_hist_info; ///< ModalAI AE data (Histogram)
    modal_exposure_msv_config_t  ae_msv_info;  ///< ModalAI AE data (MSV)
};


static const inline char* GetImageFmtString(int fmt)
{
    switch ((ImageFormat)fmt){
        case FMT_RAW8:  return "raw8";
        case FMT_RAW10: return "raw10";
        case FMT_NV12:  return "nv12";
        case FMT_NV21:  return "nv21";
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
            return HAL3_FMT_YUV;

        case FMT_TOF:

#ifdef APQ8096
            return HAL_PIXEL_FORMAT_BLOB;
#elif QRB5165
            return HAL_PIXEL_FORMAT_RAW12;
#else
    #error "Platform invalid"
#endif
        default:
            M_ERROR("ERROR: Invalid Preview Format!\n");
            throw -EINVAL;
    }
}

// Get the string associated with the type
static const inline char* GetTypeString(int type)
{
    switch ((CameraType)type){
        case CAMTYPE_OV7251:      return "ov7251";
        case CAMTYPE_OV9782:      return "ov9782";
        case CAMTYPE_IMX214:      return "imx214";
        case CAMTYPE_TOF:         return "pmd-tof";

        default:               return "Invalid";
    }
}

// Get the type associated with the string
static const inline CameraType GetCameraTypeFromString(char *type)
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

#endif // COMMON_DEFS
