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

project(QRB5165-camera-server)

cmake_minimum_required(VERSION 3.3)

option(PLATFORM "platform APQ8096 or QRB5165" QRB5165)

if(PLATFORM MATCHES APQ8096)
    message(STATUS "Building for platform APQ8096")
    add_definitions(-DAPQ8096 )
else()
    message(STATUS "Building for platform QRB5165")
    add_definitions(-DQRB5165 )
endif()

set(CMAKE_BUILD_TYPE "Release")
set(CMAKE_CXX_FLAGS "-std=c++11 -fpermissive -L/usr/lib -Wl,--unresolved-symbols=ignore-in-shared-libs ${CMAKE_CXX_FLAGS}")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -g -pthread -Wall -Wno-unused-function -Wno-format-overflow -Wno-delete-non-virtual-dtor -fPIC ${CMAKE_CXX_FLAGS_RELEASE}")
set(SERVERNAME voxl-camera-server)


if(PLATFORM MATCHES QRB5165)
set(CMAKE_CXX_FLAGS "-L/usr/lib64 ${CMAKE_CXX_FLAGS}")
endif()

set(CONFNAME camera-server-config-helper)
set(CONFSOURCE "config/camera_server_config_helper.cpp")

file(GLOB_RECURSE all_src_files "src/*.c*")
add_executable(${SERVERNAME} ${all_src_files})

include_directories(
	include/
	include/hal3/
	include/config/
	include/common/
)

if(PLATFORM MATCHES APQ8096)
    include_directories(
        proprietary/include/
    )
endif()

if(PLATFORM MATCHES APQ8096)

    FILE( GLOB_RECURSE TMP_LIBS "proprietary/lib/*.so*" )

    target_link_libraries(${SERVERNAME}
        ${TMP_LIBS}
        modal_pipe
        modal_exposure
        modal_json
        voxl_cutils
    )
else()
    target_link_libraries(${SERVERNAME}
        cutils
        modal_pipe
        modal_exposure
        modal_json
        voxl_cutils
        camera_metadata
        hardware
    )
endif()


add_executable(${CONFNAME}
    ${CONFSOURCE}
    src/config/config_file.cpp
    src/config/config_defaults.cpp
    src/common/debug_log.cpp
    src/common/common.cpp
)

target_link_libraries(${CONFNAME}
    modal_json
)

install(
    TARGETS ${SERVERNAME} ${CONFNAME}
    LIBRARY         DESTINATION /usr/lib
    RUNTIME         DESTINATION /usr/bin
    PUBLIC_HEADER   DESTINATION /usr/include
)
