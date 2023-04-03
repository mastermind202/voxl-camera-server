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

# change cmake policy to allow versioning in `project(...)`
cmake_policy(SET CMP0048 NEW)
project(QRB5165-camera-server VERSION 1.4.7)
cmake_minimum_required(VERSION 3.11)

option(PLATFORM "platform APQ8096 or QRB5165" NONE)
option(DEBUG_SYM "whether to build with debug symbols" FALSE)

if(PLATFORM MATCHES APQ8096)
    message(STATUS "[MODALAI] Building for platform APQ8096")
    add_definitions(-DAPQ8096 )
elseif(PLATFORM MATCHES QRB5165)
    message(STATUS "[MODALAI] Building for platform QRB5165")
    add_definitions(-DQRB5165 )
else()
    message(FATAL_ERROR "Platform not specified")
endif()

if (DEBUG_SYM)
    message(STATUS "[MODALAI] Building with debug configuration")
    set(CMAKE_BUILD_TYPE "Debug")

    # Include unit test framework in debug builds
    Include(FetchContent)

    FetchContent_Declare(
            Catch2
            GIT_REPOSITORY https://github.com/catchorg/Catch2.git
            GIT_TAG        v3.0.1 # or a later release
    )

    FetchContent_MakeAvailable(Catch2)
    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
    include(CTest)
    include(Catch)
else()
    message(STATUS "[MODALAI] Building with release configuration")
    set(CMAKE_BUILD_TYPE "Release")
endif()

set(CMAKE_CXX_FLAGS "-std=c++14 -L/usr/lib -Wl,--unresolved-symbols=ignore-in-shared-libs ${CMAKE_CXX_FLAGS}")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -g -pthread -Wall -Wno-unused-function -Wno-format-overflow -Wno-delete-non-virtual-dtor -fPIC ${CMAKE_CXX_FLAGS_RELEASE}")
set(CMAKE_CXX_FLAGS_DEBUG  "-g -pthread -Wall -Werror -Wno-unused-function -Wno-format-overflow -Wno-delete-non-virtual-dtor -fPIC ${CMAKE_CXX_FLAGS_DEBUG}")
set(SERVERNAME voxl-camera-server)

# We know what we're doing
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-format-truncation -Wno-pointer-arith")


if(PLATFORM MATCHES QRB5165)
set(CMAKE_CXX_FLAGS "-L/usr/lib64 ${CMAKE_CXX_FLAGS}")
endif()

set(CONFNAME camera-server-config-helper)
set(CONFSOURCE "config/camera_server_config_helper.cpp")

file(GLOB_RECURSE all_src_files "src/*.c*")
add_executable(${SERVERNAME} ${all_src_files})

# add test target for debug builds
if (DEBUG_SYM)
    file(GLOB_RECURSE all_test_files "tests/*.cpp")
    add_executable(tests src/c2d/c2d_transformer.cpp ${all_test_files})
    target_link_libraries(tests PRIVATE Catch2::Catch2WithMain)
    catch_discover_tests(tests)
endif()

include_directories(
    include/
    include/hal3/
    include/config/
    include/common/
    include/omx/
    include/tof-interface/
    /usr/include/royale/
)

target_link_libraries(${SERVERNAME}
    dl
    modal_pipe
    modal_journal
    modal_exposure
    modal_json
    voxl_cutils
    camera_metadata
    hardware
    platform
)

if(PLATFORM MATCHES APQ8096)

    target_link_libraries(${SERVERNAME}
        camera_client
        tof-interface
        spectre3
        log
    )
else()
    target_link_libraries(${SERVERNAME}
        voxl_cci_direct
        spectre
        cutils
    )
endif()


add_executable(${CONFNAME}
    ${CONFSOURCE}
    src/config/config_file.cpp
    src/config/config_defaults.cpp
)

target_link_libraries(${CONFNAME}
    modal_journal
    modal_json
)

install(
    TARGETS ${SERVERNAME} ${CONFNAME}
    LIBRARY         DESTINATION /usr/lib
    RUNTIME         DESTINATION /usr/bin
    PUBLIC_HEADER   DESTINATION /usr/include
)
