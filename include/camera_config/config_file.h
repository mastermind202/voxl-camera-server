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

#ifndef CONFIG_FILE_H
#define CONFIG_FILE_H

#include <map>
#include <iostream>
#include <stdint.h>
#include <string>
#include "common_defs.h"
#include "cJSON.h"

#define JsonPortStrings(x) (x == 0 ? JsonPortJ2String : (x == 1 ? JsonPortJ3String : JsonPortJ4String))

//------------------------------------------------------------------------------------------------------------------------------
// Class that reads from the config file. Potential support for different config file formats should be done here to keep it
// isolated from the rest of the code
//
// @todo Need to handle multiple cameras of the same type
//------------------------------------------------------------------------------------------------------------------------------
class ConfigFile
{
public:
    // Read and parse the config file given in the command line argument
    static Status Read(const char*     pConfigFileName,     ///< Config filename
                       int*            pNumCameras,         ///< Returned number of cameras detected in the config file
                       PerCameraInfo** ppPerCameraInfo);    ///< Returned camera info for each camera in the config file

    // Read and parse the config file given in the command line argument
    static Status Write(const char*     pConfigFileName,       ///< Config filename
                        int             pNumCameras,           ///< Passed number of cameras
                        PerCameraInfo   pPerCameraInfo[]);     ///< Passed camera info for each camera in the config file

    static void PrintCameraInfo(PerCameraInfo* pCameraInfo);    ///< Camera info of all cameras

private:
    // Disable instantiation of this class
    ConfigFile();
    ~ConfigFile();

    // Parse the JSON entries from the linked list represented by pJsonParent
    static Status GetCameraInfo(cJSON*          pJsonParent,       ///< Main Json linked list
                                PerCameraInfo*  pPerCameraInfo);   ///< Camera info from the config file
    static bool         IsConfigFileVersionSupported(cJSON* pJsonParent);
    static CameraType   GetCameraType(cJSON* pCameraInfo);

    // These are the main element strings in the JSON camera config file - so to speak these are variable names in the config
    // file. Each of these variables have values associated with them and the values could be integer, int64, strings etc
    static constexpr char*  JsonVersionString           = (char*)"version";                  ///< Config file version
    static constexpr char*  JsonTypeString              = (char*)"type";                     ///< Camera type
    static constexpr char*  JsonNameString              = (char*)"name";                     ///< Camera name
    static constexpr char*  JsonWidthString             = (char*)"width";                    ///< Frame width
    static constexpr char*  JsonHeightString            = (char*)"height";                   ///< Frame height
    static constexpr char*  JsonFpsString               = (char*)"frame_rate";               ///< Fps

    static constexpr char*  JsonAEDesiredMSVString      = (char*)"ae_desired_msv";           ///< Modal AE Algorithm Desired MSV
    static constexpr char*  JsonAEKPString              = (char*)"ae_k_p_ns";                ///< Modal AE Algorithm k_p
    static constexpr char*  JsonAEKIString              = (char*)"ae_k_i_ns";                ///< Modal AE Algorithm k_i
    static constexpr char*  JsonAEMaxIString            = (char*)"ae_max_i";                 ///< Modal AE Algorithm max i

    static constexpr char*  JsonOverrideIdString        = (char*)"override_id";              ///< Override id
    static constexpr char*  JsonEnabledString           = (char*)"enabled";                  ///< Is camera enabled

    static constexpr char*  DoesNotExistString          = (char*)"DoesNotExist";
    
    static std::map<std::string, int> m_sFmtMap[3];         ///< Preview format to enum mapping

    static double        m_sFileVersion;                        ///< Config file version
    static const int32_t MaxSupportedCameras = 32;              ///< Max number of cameras supported
    static PerCameraInfo m_sPerCameraInfo[MaxSupportedCameras]; ///< Per camera info
    static int32_t       m_sNumCameras;                         ///< Number of cameras
};

#endif // end #define CAMERA_CONFIG_H
