#include "gps_pose_subscriber.h"

// Position definitions
static double lat_deg = 0.0;
static double lon_deg = 0.0;
static double alt_m = 0.0;

// protect multi-byte states such as the attitude struct with this mutex
static pthread_mutex_t data_mutex = PTHREAD_MUTEX_INITIALIZER;

static void _gps_connect_cb(__attribute__((unused)) int ch, __attribute__((unused)) void* context)
{
    printf("GPS server Connected \n");
}

static void _gps_disconnect_cb(__attribute__((unused)) int ch, __attribute__((unused)) void* context)
{
    printf("Camera server Disconnected\n");
}

static void _gps_helper_cb(__attribute__((unused))int ch, char* data, int bytes, __attribute__((unused)) void* context)
{
    pthread_mutex_lock(&data_mutex);
	// validate that the data makes sense
	int n_packets;
	mavlink_message_t* msg_array = pipe_validate_mavlink_message_t(data, bytes, &n_packets);
	if(msg_array == NULL){
		return;
	}

	// grab the first one
	mavlink_message_t* msg = &msg_array[0];

	// fetch integer values from the unpacked mavlink message
	int32_t  lat  = mavlink_msg_gps_raw_int_get_lat(msg);
	int32_t  lon  = mavlink_msg_gps_raw_int_get_lon(msg);
	int32_t  alt  = mavlink_msg_gps_raw_int_get_alt(msg);

	// convert integer values to more useful units
	lat_deg = (double)lat/10000000.0;
	lon_deg = (double)lon/10000000.0;
	alt_m   = (double)alt/1000.0;
    pthread_mutex_unlock(&data_mutex);

	return;
}

int gps_data_grab_init(void)
{
	// Setup necessary pipes for GPS and pose
    pipe_client_set_connect_cb(GPS_CH, _gps_connect_cb, NULL);
    pipe_client_set_disconnect_cb(GPS_CH, _gps_disconnect_cb, NULL);
    pipe_client_set_simple_helper_cb(GPS_CH, _gps_helper_cb, NULL);
    int ret = pipe_client_open(GPS_CH, GPS_RAW_OUT_PATH, "voxl-inspect-camera-gps", \
                    EN_PIPE_CLIENT_SIMPLE_HELPER | EN_PIPE_CLIENT_AUTO_RECONNECT, \
                    MAVLINK_MESSAGE_T_RECOMMENDED_READ_BUF_SIZE);
    return ret;
}

struct gps_data grab_gps_info(void)
{
	struct gps_data gps_info;
	pthread_mutex_lock(&data_mutex);
    gps_info.latitude = lat_deg;
	gps_info.longitude = lon_deg;
	gps_info.altitude = alt_m;
    pthread_mutex_unlock(&data_mutex);
	return gps_info;
}
