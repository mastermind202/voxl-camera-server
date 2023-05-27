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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <string>
#include <list>
#include <algorithm>
#include <modal_json.h>

#include "config_file.h"
#include "config_defaults.h"
#include "common_defs.h"
#include <modal_journal.h>

using namespace std;



#define contains(a, b) (std::find(a.begin(), a.end(), b) != a.end())

// -----------------------------------------------------------------------------------------------------------------------------
// Read and parse the config file. This function can be modified to support any config file format. The information for each
// camera read from the config file is returned from this function.
//
// Note:
// "cameras" will contain memory allocated by this function and it is the callers responsibility to free/pop it it.
// -----------------------------------------------------------------------------------------------------------------------------
Status ReadConfigFile(PerCameraInfo* cameras, int* camera_len)
{
	int i, tmp, is_writing_fresh;
	std::list<int> cameraIds;
	std::list<string> cameraNames;

	const char* sensor_strings[] = SENSOR_STRINGS;
	const char* ae_strings[] = AE_STRINGS;

	cJSON* parent;
	cJSON* cameras_json;
	int numCameras;

	// caller provided a list of cameras, must be writing a new file from cam config helper
	if(camera_len>0){
		is_writing_fresh = 1;
		numCameras = camera_len;
		remove(CONFIG_FILE_NAME);
		parent = cJSON_CreateObject();
		cJSON_AddNumberToObject(parent, "version", CURRENT_VERSION);
		cameras_json = json_fetch_array_and_add_if_missing(parent, "cameras", &tmp);
	}
	// normal reading mode
	else{
		is_writing_fresh = 0;
		parent = json_read_file(CONFIG_FILE_NAME);
		if(parent==NULL){
			M_ERROR("missing config file\n");
			return S_ERROR;
		}
		cameras_json = json_fetch_array_and_add_if_missing(parent, "cameras", &numCameras);
	}

	// sanity check
	if(numCameras<1 || numCameras > 7){
		fprintf(stderr, "array of cameras should be between 1 and 7\n");
		return S_ERROR;
	}

	// now go through all the cameras, preset or empty
	for(i=0; i<numCameras; i++){

		PerCameraInfo* cam = &cameras[i];

		cJSON* item = cJSON_GetArrayItem(cameras_json, i);
		if(item==NULL){
			M_ERROR("failed to fetch item %d from json array\n", i);
			goto ERROR_EXIT;
		}

		if(!is_writing_fresh){
			// start with an empty caminfo struct for each camera in the config
			*cam = getDefaultCameraInfo(SENSOR_INVALID);
		}

		// if writing fresh, this name will have been set by the config helper
		if(json_fetch_enum_with_default(item, "type", (int*)&cam->type, sensor_strings, SENSOR_MAX_TYPES, (int)cam->type)){
			goto ERROR_EXIT;
		}

		// if not writing fresh, set the whole cam info struct to default
		if(!is_writing_fresh){
			*cam = getDefaultCameraInfo(cam->type);
			printf("type: %d\n", cam->type);
		}

		if(json_fetch_string_with_default(item, "name", cam->name, 63, cam->name)){
			M_ERROR("Reading config file: camera name not specified\n", cam->name);
			goto ERROR_EXIT;
		}

		// record the camera name separately to make sure there are no duplicates
		if(contains(cameraNames, cam->name)){
			M_ERROR("Reading config file: multiple cameras with name: %s\n", cam->name);
			goto ERROR_EXIT;
		}
		cameraNames.push_back(cam->name);
		printf("name: %s\n", cam->name);

		if(json_fetch_int_with_default(item, "camera_id", &(cam->camId), cam->camId)){
			M_ERROR("Reading config file: camera id not specified for: %s\n", cam->name);
			goto ERROR_EXIT;
		}
		printf("id: %d\n", cam->camId);

		// record the cam id and make sure there are no duplicates
		if(contains(cameraIds, cam->camId)){
			M_ERROR("Reading config file: multiple cameras with id: %d\n", cam->camId);
			goto ERROR_EXIT;
		}
		cameraIds.push_back(cam->camId);


		if(!cJSON_HasObjectItem(item, "camera_id_second") || json_fetch_int(item, "camera_id_second", &(cam->camId2))){
			M_DEBUG("No secondary id found for camera: %s, assuming mono\n", cam->name);
		}
		if(contains(cameraIds, cam->camId2)){
			M_ERROR("Reading config file: multiple cameras with id: %d\n", cam->camId);
			goto ERROR_EXIT;
		}

		// record the second camid for stereo cams
		if(cam->camId2>0){
			M_DEBUG("Secondary id found for camera: %s, assuming stereo\n", cam->name);
			cameraIds.push_back(cam->camId2);
			json_fetch_bool_with_default(item, "independent_exposure",  (int*)&cam->ind_exp, cam->ind_exp);
		}

		json_fetch_int_with_default(item, "fps" , &cam->fps, cam->fps);
		json_fetch_bool_with_default(item, "enabled", (int*)&cam->isEnabled, cam->isEnabled);
		printf("ENABLED: %d\n", cam->isEnabled);


		// now we parse the 4 streams, preview, small, large video, and snapshot
		// only populate and parse if enabled by default or explicitly set by the user
		if(cJSON_GetObjectItem(item, "en_preview")!=NULL || cam->en_preview){
			json_fetch_bool_with_default (item, "en_preview",         &cam->en_preview,  cam->en_preview);
			json_fetch_int_with_default  (item, "preview_height",     &cam->pre_height,  cam->pre_height);
			json_fetch_int_with_default  (item, "preview_width",      &cam->pre_width,   cam->pre_width);
		}

		if(cJSON_GetObjectItem(item, "en_small_video")!=NULL || cam->en_small_video){
			json_fetch_bool_with_default (item, "en_small_video",      &cam->en_small_video,      cam->en_small_video);
			json_fetch_int_with_default  (item, "small_video_height",  &cam->small_video_height,  cam->small_video_height);
			json_fetch_int_with_default  (item, "small_video_width",   &cam->small_video_width,   cam->small_video_width);
			json_fetch_int_with_default  (item, "small_video_bitrate", &cam->small_video_bitrate, cam->small_video_bitrate);
		}

		if(cJSON_GetObjectItem(item, "en_large_video")!=NULL || cam->en_large_video){
			json_fetch_bool_with_default (item, "en_large_video",      &cam->en_large_video,      cam->en_large_video);
			json_fetch_int_with_default  (item, "large_video_width",   &cam->large_video_height,  cam->large_video_height);
			json_fetch_int_with_default  (item, "large_video_height",  &cam->large_video_width,   cam->large_video_width);
			json_fetch_int_with_default  (item, "large_video_bitrate", &cam->large_video_bitrate, cam->large_video_bitrate);
		}

		if(cJSON_GetObjectItem(item, "en_snapshot")!=NULL || cam->en_large_video){
			json_fetch_bool_with_default (item, "en_snapshot",        &cam->en_snapshot, cam->en_snapshot);
			json_fetch_int_with_default  (item, "en_snapshot_width",  &cam->snap_width,  cam->snap_width);
			json_fetch_int_with_default  (item, "en_snapshot_height", &cam->snap_height, cam->snap_height);
		}

		if(json_fetch_enum_with_default(item, "ae_mode", (int*)&cam->ae_mode, ae_strings, AE_MAX_MODES, (int)cam->ae_mode)){
			goto ERROR_EXIT;
		}

		// only load histogram settings if enabled (not used by default anymore)
		if(cam->ae_mode == AE_LME_HIST){
			json_fetch_float_with_default (item, "ae_desired_msv", &cam->ae_hist_info.desired_msv, cam->ae_hist_info.desired_msv);
			json_fetch_float_with_default (item, "ae_k_p_ns",      &cam->ae_hist_info.k_p_ns,      cam->ae_hist_info.k_p_ns);
			json_fetch_float_with_default (item, "ae_k_i_ns",      &cam->ae_hist_info.k_i_ns,      cam->ae_hist_info.k_i_ns);
			json_fetch_float_with_default (item, "ae_max_i",       &cam->ae_hist_info.max_i,       cam->ae_hist_info.max_i);
		}

		// only load msv settings if enabled (default for all but hires cams)
		if(cam->ae_mode == AE_LME_MSV){
			json_fetch_float_with_default (item, "ae_desired_msv",     &cam->ae_msv_info.desired_msv,                       cam->ae_msv_info.desired_msv);
			json_fetch_float_with_default (item, "ae_filter_alpha",    &cam->ae_msv_info.msv_filter_alpha,                  cam->ae_msv_info.msv_filter_alpha);
			json_fetch_float_with_default (item, "ae_ignore_fraction", &cam->ae_msv_info.max_saturated_pix_ignore_fraction, cam->ae_msv_info.max_saturated_pix_ignore_fraction);
			json_fetch_float_with_default (item, "ae_slope",           &cam->ae_msv_info.exposure_gain_slope,               cam->ae_msv_info.exposure_gain_slope);
			json_fetch_int_with_default   (item, "ae_exposure_period", (int*)&cam->ae_msv_info.exposure_update_period,      (int)cam->ae_msv_info.exposure_update_period);
			json_fetch_int_with_default   (item, "ae_gain_period",     (int*)&cam->ae_msv_info.gain_update_period,          (int)cam->ae_msv_info.gain_update_period);
		}

		// standby settings for tof only
		if(cam->type == SENSOR_TOF){
			json_fetch_bool_with_default(item, "standby_enabled", (int*)&cam->standby_enabled, cam->standby_enabled);
			json_fetch_int_with_default  (item, "decimator", &cam->decimator,   cam->decimator);
		}

	} // end of loop through cameras


	// check if we got any errors in that process
	if(json_get_parse_error_flag()){
		fprintf(stderr, "failed to parse data in %s\n", CONFIG_FILE_NAME);
		cJSON_Delete(parent);
		return S_ERROR;
	}

	if(json_get_modified_flag()){
		json_write_to_file_with_header(CONFIG_FILE_NAME, parent, CONFIG_FILE_HEADER);
	}

	cJSON_free(parent);
	return S_OK;

ERROR_EXIT:

	cJSON_free(parent);
	return S_ERROR;

}

