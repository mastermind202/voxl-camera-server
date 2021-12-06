#******************************************************************************
# Copyright 2020 ModalAI Inc.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice,
#    this list of conditions and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
#
# 3. Neither the name of the copyright holder nor the names of its contributors
#    may be used to endorse or promote products derived from this software
#    without specific prior written permission.
#
# 4. The Software is used solely in conjunction with devices provided by
#    ModalAI Inc.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.
#*****************************************************************************/

cmake_minimum_required(VERSION 3.3)

set(CMAKE_BUILD_TYPE "Release")
set(CMAKE_CXX_FLAGS "-std=c++11 -fpermissive -L/usr/lib64")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -g -pthread -Wall -fPIC")
set(SERVERNAME rb5-camera-server)

set(CONFNAME camera-server-config-helper)
set(CONFSOURCE "config/camera_server_config_helper.cpp")

file(GLOB_RECURSE all_src_files "src/*.c*")
add_executable(${SERVERNAME} ${all_src_files})

include_directories(
	include/
	include/hal3/
	include/camera_config/
	include/common/
    hardware_headers/
)

target_link_libraries(${SERVERNAME}
	/usr/lib/libcutils.so.0
	/usr/lib/libutils.so.0
	/usr/lib/liblog.so.0
	/usr/lib/libhardware.so.0
	/usr/lib/libcamera_metadata.so.0
	/usr/lib/libbinder.so.0
    dl
	modal_pipe
	modal_json
	/usr/lib64/libmodal_exposure.so
	voxl_cutils
	mv3
	gsl
	llvm-qcom
	CB

)

install(
	TARGETS ${SERVERNAME}
	LIBRARY			DESTINATION /usr/lib
	RUNTIME			DESTINATION /usr/bin
	PUBLIC_HEADER	DESTINATION /usr/include
)
