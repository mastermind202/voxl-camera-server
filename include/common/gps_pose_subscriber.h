#include <c_library_v2/common/mavlink.h>
#include <c_library_v2/development/development.h>

#include <modal_journal.h>
#include <modal_pipe.h>
#include <modal_start_stop.h>
#include <modal_pipe_client.h>

#define GPS_RAW_OUT_PATH	(MODAL_PIPE_DEFAULT_BASE_DIR "mavlink_gps_raw_int/")
#define LOCAL_POSE_PATH 	(MODAL_PIPE_DEFAULT_BASE_DIR "mavlink_local_position_ned/")
#define DEG_TO_RAD	(3.14159265358979323846/180.0)
#define RAD_TO_DEG	(180.0/3.14159265358979323846)

#define GPS_CH  4
#define POSE_CH 5

// Position definitions
static double lat_deg = 0.0;
static double lon_deg = 0.0;
static double alt_m = 0.0;
static double x = 0.0;
static double y = 0.0;
static double z = 0.0;
static float roll = 0.0;
static float pitch = 0.0;
static float yaw = 0.0;

// protect multi-byte states such as the attitude struct with this mutex
static pthread_mutex_t data_mutex = PTHREAD_MUTEX_INITIALIZER;

struct gps_data{
	double latitude;
	double longitude;
	double altitude;
};

struct pose_data {
	double x_val;
	double y_val;
	double z_val;
	float roll_val;
	float pitch_val;
	float yaw_val;
};

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

static void _rotation_to_tait_bryan(float R[3][3], float* roll, float* pitch, float* yaw)
{
	*roll  = atan2(R[2][1], R[2][2]);
	*pitch = asin(-R[2][0]);
	*yaw   = atan2(R[1][0], R[0][0]);

	if(fabs((double)*pitch - M_PI_2) < 1.0e-3){
		*roll = 0.0;
		*pitch = atan2(R[1][2], R[0][2]);
	}
	else if(fabs((double)*pitch + M_PI_2) < 1.0e-3) {
		*roll = 0.0;
		*pitch = atan2(-R[1][2], -R[0][2]);
	}
	return;
}

static void _local_pose_connect_cb(__attribute__((unused)) int ch, __attribute__((unused)) void* context)
{
    printf("Local Pose server Connected \n");
}

static void _local_pose_disconnect_cb(__attribute__((unused)) int ch, __attribute__((unused)) void* context)
{
    printf("Local Pose server Disconnected\n");
}

static void _local_pose_connect_helper_cb(__attribute__((unused))int ch, char* data, int bytes, __attribute__((unused)) void* context)
{
    pthread_mutex_lock(&data_mutex);
	// validate that the data makes sense
	int n_packets;
	pose_vel_6dof_t* d = pipe_validate_pose_vel_6dof_t(data, bytes, &n_packets);

    // convert rotation to tait-bryan angles in degrees for easier viewing
    for(int i = 0; i < n_packets; i++){
        _rotation_to_tait_bryan(d[i].R_child_to_parent, &roll, &pitch, &yaw);
        roll	*= (float)RAD_TO_DEG;
        pitch	*= (float)RAD_TO_DEG;
        yaw		*= (float)RAD_TO_DEG;
        x = (double)d[i].T_child_wrt_parent[0];
        y = (double)d[i].T_child_wrt_parent[1];
        z = (double)d[i].T_child_wrt_parent[2];
    }
    pthread_mutex_unlock(&data_mutex);
    return;
}

static struct gps_data grab_gps_info(void){
	struct gps_data gps_info;
	pthread_mutex_lock(&data_mutex);
    gps_info.latitude = lat_deg;
	gps_info.longitude = lon_deg;
	gps_info.altitude = alt_m;
    pthread_mutex_unlock(&data_mutex);
	return gps_info;
}

static struct pose_data grab_pose_info(void){
	struct pose_data pose_info;
    pthread_mutex_lock(&data_mutex);
	pose_info.x_val = x;
	pose_info.y_val = y;
	pose_info.z_val = z;
	pose_info.roll_val = roll;
	pose_info.pitch_val = pitch;
	pose_info.yaw_val = yaw;
    pthread_mutex_unlock(&data_mutex);
	return pose_info;
}
