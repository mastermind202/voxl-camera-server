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

#include <string.h>
#include <ctype.h>

#include "config_defaults.h"
#include "common_defs.h"

// small video stream is usually for rtsp
#define RTSP_BITRATE_DEFAULT 3000000 // mbps

static const PerCameraInfo emptyDefaults =
    {
        "",                         //< Name
        SENSOR_INVALID,             //Sensor
        -1,                         //ID
        -1,                         //ID2
        0,                          //Enabled?
        -1,                         //Framerate
        0,                          //< Enable Preview Mode?
        -1,                         //< Preview Width of the frame
        -1,                         //< Preview Height of the frame
        FMT_INVALID,                //< Preview Frame format
        0,                          //< Enable Small Video
        -1,                         //< Small Video Width of the frame
        -1,                         //< Small Video Height of the frame
        -1,                         //< Small Video Bitrate
        0,                          //< Enable Large Video
        -1,                         //< Large Video Width of the frame
        -1,                         //< Large Video Height of the frame
        -1,                         //< Large Video Bitrate
        0,                          //< Enable Snapshot mode?
        -1,                         //< Snapshot Width of the frame
        -1,                         //< Snapshot Height of the frame
        0,                          //< Independent Exposure
        AE_OFF,                     //< AE Mode
        0,                          //< Standby Enabled
        1,                          //< Standby Decimator
    };


static const PerCameraInfo OV7251Defaults = 
    {
        "",                         //< Name
        SENSOR_OV7251,              //< Sensor
        -1,                         //< ID
        -1,                         //< ID2
        1,                          //< Enabled?
        30,                         //< Framerate
        1,                          //< Enable Preview Mode?
        640,                        //< Preview Width of the frame
        480,                        //< Preview Height of the frame
        FMT_RAW8,                   //< Preview Frame format
        0,                          //< Enable Small Video
        -1,                         //< Small Video Width of the frame
        -1,                         //< Small Video Height of the frame
        -1,                         //< Small Video Bitrate
        0,                          //< Enable Large Video
        -1,                         //< Large Video Width of the frame
        -1,                         //< Large Video Height of the frame
        -1,                         //< Large Video Bitrate
        0,                          //< Enable Snapshot mode?
        -1,                         //< Snapshot Width of the frame
        -1,                         //< Snapshot Height of the frame
        0,                          //< Independent Exposure
        AE_LME_MSV,                 //< AE Mode
        {                           //< Hist AE Algorithm Parameters
            100,                    //< Gain Min
            1000,                   //< Gain Max
            20,                     //< Exposure Min
            33000,                  //< Exposure Max
            64.0,                   //< Desired MSV
            8000.0,                 //< k_p_ns
            5.0,                    //< k_i_ns
            250.0,                  //< Max i
            3,                      //< p Good Threshold
            1,                      //< Exposure Period
            2,                      //< Gain Period
            0,                      //< Display Debug
            8000,                   //< Exposure offset
        },
        {                           //< MSV AE Algorithm Parameters
            101,                    //< Gain Min
            835,                    //< Gain Max
            20,                     //< Exposure Min
            33000,                  //< Exposure Max
            5000,                   //< Soft min exposure
            0.05,                   //< Gain Slope
            60.0,                   //< Desired MSV
            0.6,                    //< Filter Alpha
            0.2,                    //< Most saturated ignore frac
            1,                      //< Exposure update period
            1,                      //< Gain update period
            false                   //< Display Debug
        }
    };


static const PerCameraInfo OV9782Defaults =
    {
        "",                         //< Name
        SENSOR_OV9782,              //< Sensor
        -1,                         //< ID
        -1,                         //< ID2
        1,                          //< Enabled?
        30,                         //< Framerate
        1,                          //< Enable Preview Mode?
        1280,                       //< Preview Width of the frame
        800,                        //< Preview Height of the frame
        FMT_NV21,                   //< Preview Frame format
        0,                          //< Enable Small Video
        -1,                         //< Small Video Width of the frame
        -1,                         //< Small Video Height of the frame
        -1,                         //< Small Video Bitrate
        0,                          //< Enable Large Video
        -1,                         //< Large Video Width of the frame
        -1,                         //< Large Video Height of the frame
        -1,                         //< Large Video Bitrate
        0,                          //< Enable Snapshot mode?
        -1,                         //< Snapshot Width of the frame
        -1,                         //< Snapshot Height of the frame
        0,                          //< Independent Exposure
        AE_LME_MSV,                 //< AE Mode
        {                           //< Hist AE Algorithm Parameters
            54,                     //< Gain Min
            835,                    //< Gain Max
            20,                     //< Exposure Min
            33000,                  //< Exposure Max
            54.0,                   //< Desired MSV
            8000.0,                 //< k_p_ns
            5.0,                    //< k_i_ns
            250.0,                  //< Max i
            3,                      //< p Good Threshold
            1,                      //< Exposure Period
            2,                      //< Gain Period
            0,                      //< Display Debug
            8000,                   //< Exposure offset
        },
        {                           //< MSV AE Algorithm Parameters
            54,                     //< Gain Min
            835,                    //< Gain Max
            20,                     //< Exposure Min
            33000,                  //< Exposure Max
            5000,                   //< Soft min exposure
            0.05,                   //< Gain Slope
            60.0,                   //< Desired MSV
            0.6,                    //< Filter Alpha
            0.2,                    //< Most saturated ignore frac
            1,                      //< Exposure update period
            1,                      //< Gain update period
            false                   //< Display Debug
        }
    };


static const PerCameraInfo IMX214Defaults =
    {
        "",                         //< Name
        SENSOR_IMX214,              //< Sensor
        -1,                         //< ID
        -1,                         //< ID2
        1,                          //< Enabled?
        30,                         //< Framerate
        0,                          //< Enable Preview Mode?
        640,                        //< Preview Width of the frame
        480,                        //< Preview Height of the frame
        FMT_NV21,                   //< Preview Frame format
        1,                          //< Enable Small Video
        1024,                       //< Small Video Width of the frame
        768,                        //< Small Video Height of the frame
        RTSP_BITRATE_DEFAULT,       //< Small Video Bitrate
        1,                          //< Enable Large Video
        4096,                       //< Large Video Width of the frame
        2160,                       //< Large Video Height of the frame
        120000000,                  //< Large Video Bitrate
        1,                          //< Enable Snapshot mode?
        4160,                       //< Snapshot Width of the frame
        3120,                       //< Snapshot Height of the frame
        0,                          //< Independent Exposure
        AE_ISP,                     //< AE Mode
        {                           //< Hist AE Algorithm Parameters
            100,                    //< Gain Min
            1000,                   //< Gain Max
            20,                     //< Exposure Min
            33000,                  //< Exposure Max
            54.0,                   //< Desired MSV
            8000.0,                 //< k_p_ns
            5.0,                    //< k_i_ns
            250.0,                  //< Max i
            3,                      //< p Good Threshold
            1,                      //< Exposure Period
            2,                      //< Gain Period
            0,                      //< Display Debug
            8000,                   //< Exposure offset
        }
    };


static const PerCameraInfo IMX412Defaults =
    {
        "",                         //< Name
        SENSOR_IMX412,              //< Sensor
        -1,                         //< ID
        -1,                         //< ID2
        1,                          //< Enabled?
        30,                         //< Framerate
        0,                          //< Enable Preview Mode?
        640,                        //< Preview Width of the frame
        480,                        //< Preview Height of the frame
        FMT_NV21,                   //< Preview Frame format
        1,                          //< Enable Small Video
        1024,                       //< Small Video Width of the frame
        768,                        //< Small Video Height of the frame
        RTSP_BITRATE_DEFAULT,       //< Small Video Bitrate
        1,                          //< Enable Large Video
        2048,                       //< Large Video Width of the frame
        1536,                       //< Large Video Height of the frame
        120000000,                  //< Large Video Bitrate
        1,                          //< Enable Snapshot mode?
        3840,                       //< Snapshot Width of the frame
        2160,                       //< Snapshot Height of the frame
        0,                          //< Independent Exposure
        AE_ISP,                     //< AE Mode
        {                           //< Hist AE Algorithm Parameters
            100,                    //< Gain Min
            1000,                   //< Gain Max
            20,                     //< Exposure Min
            33000,                  //< Exposure Max
            54.0,                   //< Desired MSV
            8000.0,                 //< k_p_ns
            5.0,                    //< k_i_ns
            250.0,                  //< Max i
            3,                      //< p Good Threshold
            1,                      //< Exposure Period
            2,                      //< Gain Period
            0,                      //< Display Debug
            8000,                   //< Exposure offset
        }
    };


static const PerCameraInfo IMX678Defaults =
    {
        "",                         //< Name
        SENSOR_IMX678,              //< Sensor
        -1,                         //< ID
        -1,                         //< ID2
        1,                          //< Enabled?
        30,                         //< Framerate
        0,                          //< Enable Preview Mode?
        640,                        //< Preview Width of the frame
        480,                        //< Preview Height of the frame
        FMT_NV21,                   //< Preview Frame format
        1,                          //< Enable Small Video
        1024,                       //< Small Video Width of the frame
        768,                        //< Small Video Height of the frame
        RTSP_BITRATE_DEFAULT,       //< Small Video Bitrate
        1,                          //< Enable Large Video
        2048,                       //< Large Video Width of the frame
        1536,                       //< Large Video Height of the frame
        120000000,                  //< Large Video Bitrate
        1,                          //< Enable Snapshot mode?
        3840,                       //< Snapshot Width of the frame
        2160,                       //< Snapshot Height of the frame
        0,                          //< Independent Exposure
        AE_ISP,                     //< AE Mode
        {                           //< Hist AE Algorithm Parameters
            100,                    //< Gain Min
            1000,                   //< Gain Max
            20,                     //< Exposure Min
            33000,                  //< Exposure Max
            54.0,                   //< Desired MSV
            8000.0,                 //< k_p_ns
            5.0,                    //< k_i_ns
            250.0,                  //< Max i
            3,                      //< p Good Threshold
            1,                      //< Exposure Period
            2,                      //< Gain Period
            0,                      //< Display Debug
            8000,                   //< Exposure offset
        }
    };


static const PerCameraInfo TOFDefaults =
    {
        "",                         //Name
        SENSOR_TOF,                 //< Sensor
        -1,                         //< ID
        -1,                         //< ID2
        1,                          //< Enabled?
        5,                          //< Framerate
        1,                          //< Enable Preview Mode?
        224,                        //< Preview Width of the frame
        1557,                       //< Preview Height of the frame
        FMT_TOF,                    //< Preview Frame format
        0,                          //< Enable Small Video
        -1,                         //< Small Video Width of the frame
        -1,                         //< Small Video Height of the frame
        -1,                         //< Small Video Bitrate
        0,                          //< Enable Large Video
        -1,                         //< Large Video Width of the frame
        -1,                         //< Large Video Height of the frame
        -1,                         //< Large Video Bitrate
        0,                          //< Enable Snapshot mode?
        -1,                         //< Snapshot Width of the frame
        -1,                         //< Snapshot Height of the frame
        0,                          //< Independent Exposure
        AE_OFF,                     //< AE Mode
        0,                          //< Standby Enabled
        5,                          //< Standby Decimator
    };



const PerCameraInfo getDefaultCameraInfo(sensor_t t) {
    switch(t){
        case SENSOR_OV7251:
            return OV7251Defaults;
        case SENSOR_OV9782:
            return OV9782Defaults;
        case SENSOR_IMX214:
            return IMX214Defaults;
        case SENSOR_IMX412:
            return IMX412Defaults;
        case SENSOR_IMX678:
            return IMX678Defaults;
        case SENSOR_TOF:
            return TOFDefaults;

        default:
            return emptyDefaults;
    }
}
