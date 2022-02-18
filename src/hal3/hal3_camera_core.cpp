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
#include "common_defs.h"
#include "debug_log.h"
#include "hal3_camera.h"

// -----------------------------------------------------------------------------------------------------------------------------
// Get the camera module (and initialize it if it hasn't been)
// -----------------------------------------------------------------------------------------------------------------------------
camera_module_t* HAL3_get_camera_module()
{

	static camera_module_t* cameraModule = NULL;

	if(cameraModule != NULL){
		return cameraModule;
	}

	int i;
    for (i = 0;
    	 i <= NUM_MODULE_OPEN_ATTEMPTS && hw_get_module(CAMERA_HARDWARE_MODULE_ID, (const hw_module_t**)&cameraModule);
    	 i++)
    {

        VOXL_LOG_ERROR("ERROR: Camera module not opened, %d attempts remaining\n",
                        NUM_MODULE_OPEN_ATTEMPTS-i+1);

        sleep(1);

    }

	if(cameraModule == NULL){
        VOXL_LOG_FATAL("ERROR: Camera module not opened after %d attempts", NUM_MODULE_OPEN_ATTEMPTS);
		return NULL;
	}

    VOXL_LOG_INFO("SUCCESS: Camera module opened on attempt %d\n", i);

    //This check should never fail but we should still make it
    if (cameraModule->init != NULL)
    {
        cameraModule->init();
    }

    return cameraModule;

}

bool HAL3_is_config_supported(int camId, int width, int height, int format)
{

	camera_info halCameraInfo;
    camera_module_t* cameraModule = HAL3_get_camera_module();

    if(cameraModule == NULL){
        printf("ERROR: %s : Could not open camera module\n", __FUNCTION__);

        return false;
    }

	cameraModule->get_camera_info(camId, &halCameraInfo);


    camera_metadata_t* pStaticMetadata = (camera_metadata_t *)halCameraInfo.static_camera_characteristics;
    camera_metadata_ro_entry entry;

    // Get the list of all stream resolutions supported and then go through each one of them looking for a match
    int status = find_camera_metadata_ro_entry(pStaticMetadata, ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS, &entry);

    if ((0 == status) && (0 == (entry.count % 4)))
    {
        for (size_t i = 0; i < entry.count; i+=4)
        {
            if ((entry.data.i32[i]   == format) &&
            	(entry.data.i32[i+1] == width ) &&
                (entry.data.i32[i+2] == height) &&
            	(entry.data.i32[i+3] == ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT))
            {
            	return true;
            }
        }
    }

    return false;
}

void HAL3_print_camera_resolutions(int camId){

	camera_module_t* cameraModule = HAL3_get_camera_module();

	if(cameraModule == NULL){
		printf("ERROR: %s : Could not open camera module\n", __FUNCTION__);
		return;
	}

	if(camId == -1){
	    int numCameras = cameraModule->get_number_of_cameras();

	    printf("Number of cameras: %d\n\n", numCameras);

		for(int i = 0; i < numCameras; i++){
            HAL3_print_camera_resolutions(i);
	    }
	} else {
		camera_info cameraInfo;
		cameraModule->get_camera_info(camId, &cameraInfo);

        camera_metadata_t* pStaticMetadata = (camera_metadata_t *)cameraInfo.static_camera_characteristics;
        camera_metadata_ro_entry entry;

        // Get the list of all stream resolutions supported and then go through each one of them looking for a match
        find_camera_metadata_ro_entry(pStaticMetadata, ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS, &entry);

        printf("Available resolutions for camera: %d:\n", camId);
        for (size_t j = 0; j < entry.count; j+=4)
        {
            if (entry.data.i32[j + 3] == ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT)
            {
            	if(camId == 3){
                	if(entry.data.i32[j] == HAL_PIXEL_FORMAT_YCbCr_420_888){
                    	printf("\t%d x %d\n", entry.data.i32[j+1], entry.data.i32[j+2]);
                	}

            	} else {
                	if(entry.data.i32[j] == HAL_PIXEL_FORMAT_RAW10){
                    	printf("\t%d x %d\n", entry.data.i32[j+1], entry.data.i32[j+2]);
                	}
                }
            }
        }

	}

}
