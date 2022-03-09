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

#include "config_defaults.h"
#include "common_defs.h"

static const PerCameraInfo OV7251Defaults = 
    {
        "",                         //Name
        CAMTYPE_OV7251,             //Type
		true,                       //Is mono?
        -1,                         //ID
        -1,                         //ID2
		true,                       //Enabled?
        30,                         //Framerate
        640,                		//Width
        480,                		//Height
        FMT_RAW8,   				//Format

        {                       	//ModalAI AE Algorithm Parameters
            0,                  	//Gain Min
            1000,               	//Gain Max
            1000,                 	//Exposure Min
            33000,              	//Exposure Max
            68.0,               	//Desired MSV
            32000.0,            	//k_p_ns
            20.0,               	//k_i_ns
            250.0,              	//Max i
            3,                  	//p Good Threshold
            1,                  	//Exposure Period
            2,                  	//Gain Period
            false,               	//Display Debug
            8000,               	//Exposure offset
        }
    };

static const PerCameraInfo IMX214Defaults = 
    {
        "",                         //Name
        CAMTYPE_IMX214,             //Type
		true,                       //Is mono?
        -1,                         //ID
        -1,                         //ID2
		true,                       //Enabled?
        30,                         //Framerate
        640,                		//Width
        480,                		//Height
        FMT_NV21,   				//Format

        {                       	//ModalAI AE Algorithm Parameters
            0,                  	//Gain Min
            1000,               	//Gain Max
            1000,                 	//Exposure Min
            33000,              	//Exposure Max
            58.0,               	//Desired MSV
            32000.0,            	//k_p_ns
            20.0,               	//k_i_ns
            250.0,              	//Max i
            3,                  	//p Good Threshold
            1,                  	//Exposure Period
            2,                  	//Gain Period
            false,               	//Display Debug
            8000,               	//Exposure offset
        }
    };

static const PerCameraInfo emptyDefaults = 
    {
        "",                         //Name
        CAMTYPE_INVALID,            //Type
		false,                      //Is mono?
        -1,                         //ID
        -1,                         //ID2
		false,                      //Enabled?

        -1,                         //Framerate
        -1,                 		//Width
        -1,                 		//Height
        FMT_INVALID,				//Format

        {                       	//ModalAI AE Algorithm Parameters
            0,                  	//Gain Min
            0,                  	//Gain Max
            0,                  	//Exposure Min
            0,                  	//Exposure Max
            0,                  	//Desired MSV
            0,                  	//k_p_ns
            0,                  	//k_i_ns
            0,                  	//Max i
            0,                  	//p Good Threshold
            0,                  	//Exposure Period
            0,                  	//Gain Period
            0,                  	//Display Debug
            0,                  	//Exposure offset
        }
    };

const PerCameraInfo getDefaultCameraInfo(CameraType t) {
    switch(t){
        case CAMTYPE_OV7251:
            return OV7251Defaults;
        case CAMTYPE_IMX214:
            return IMX214Defaults;
        default:
            return emptyDefaults;
    }
}
