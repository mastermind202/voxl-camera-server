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

#ifndef __EXPGAIN_INTERFACE_MODALAI__
#define __EXPGAIN_INTERFACE_MODALAI__

#include "common_defs.h"
#include "exposure-hist.h"

using namespace std;

//------------------------------------------------------------------------------------------------------------------------------
// Main interface class to manage all the cameras
//------------------------------------------------------------------------------------------------------------------------------
class ExpGainModalAI {
public:
    ExpGainModalAI(
	    uint32_t           width,               ///< Width of the frame
	    uint32_t           height,              ///< Height of the frame
	    uint32_t           format,              ///< Format of the frame
	    uint32_t           strideInPixels,      ///< Stride in pixels
	    const void*        pAlgoSpecificData,   ///< Void pointer to algorithm specific data
	    const char*        cameraType           ///< String type of camera to derive pipe/interface name
    ) :
    	m_width(width),
    	m_height(height),
    	m_format(format),
    	m_strideInPixels(strideInPixels)
     { 
    	API = new ModalExposureHist(*((modal_exposure_config_t*)(pAlgoSpecificData)));
    	if (API == NULL){
    		throw -1;
    	}
    }
    ~ExpGainModalAI() {
	    if(API != NULL){
	        delete API;
	    }
    };
    // Add a frame to be used to do the exposure/gain
    Status GetNewExpGain(
        const uint8_t* pFramePixels,
        uint32_t *exposure,
    	int16_t *gain);

private:

    const uint32_t           m_width;               ///< Width of the frame
    const uint32_t           m_height;              ///< Height of the frame
    const uint32_t           m_format;              ///< Format of the frame
    const uint32_t           m_strideInPixels;      ///< Stride in pixels

    // Histogram exposure module
    ModalExposureHist*    API;
};

#endif // __EXPGAIN_INTERFACE_INTERNAL__
