#pragma once

#include <c_library_v2/common/mavlink.h>
#include <c_library_v2/development/development.h>

#include <modal_journal.h>
#include <modal_pipe.h>
#include <modal_start_stop.h>
#include <modal_pipe_client.h>

#define GPS_RAW_OUT_PATH	(MODAL_PIPE_DEFAULT_BASE_DIR "mavlink_gps_raw_int/")
#define GPS_CH  4

struct gps_data{
	double latitude;
	double longitude;
	double altitude;
};

int gps_data_grab_init(void);
struct gps_data grab_gps_info(void);
int gps_data_grab_close(void);
