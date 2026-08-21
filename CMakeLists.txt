# name: xiaorun
# email: 15610499173@163.com

cmake_minimum_required(VERSION 3.16)
project(rk3588_sop LANGUAGES CXX)

# 导出 compile_commands.json 以便语言服务器（clangd / MS C++）能解析代码跳转与补全
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ENABLE_RKNN "启用 RKNN YOLOv8 后端" OFF)
option(ENABLE_ORBBEC "启用奥比中光 SDK 输入" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenCV QUIET)
find_package(Threads REQUIRED)

if(ENABLE_ORBBEC)
  set(ORBBEC_SDK_ROOT "" CACHE PATH "Orbbec SDK 根目录，指向 OrbbecSDK_v*/SDK 或其上级目录")
  find_package(OrbbecSDK QUIET
    PATHS
      "${ORBBEC_SDK_ROOT}"
      "${ORBBEC_SDK_ROOT}/SDK"
      "${CMAKE_CURRENT_SOURCE_DIR}/third_party/orbbec_sdk"
      "${CMAKE_CURRENT_SOURCE_DIR}/third_party/OrbbecSDK"
  )
  if(NOT OrbbecSDK_FOUND)
    if(ORBBEC_SDK_ROOT STREQUAL "")
      set(ORBBEC_SDK_CANDIDATE_ROOTS
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/orbbec_sdk"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/OrbbecSDK"
        "/opt/orbbec/OrbbecSDK_v1.10.27/SDK"
        "/opt/orbbec/OrbbecSDK_v1.10.27"
        "/usr/local/orbbec/OrbbecSDK_v1.10.27/SDK"
        "/usr/local/orbbec/OrbbecSDK_v1.10.27"
      )
    else()
      set(ORBBEC_SDK_CANDIDATE_ROOTS "${ORBBEC_SDK_ROOT}")
    endif()

    foreach(ORBBEC_CANDIDATE_ROOT ${ORBBEC_SDK_CANDIDATE_ROOTS})
      if(ORBBEC_SDK_INCLUDE_DIR AND ORBBEC_SDK_LIBRARY_DIR)
        break()
      endif()
      if(EXISTS "${ORBBEC_CANDIDATE_ROOT}/include/libobsensor/ObSensor.hpp")
        set(ORBBEC_SDK_INCLUDE_DIR "${ORBBEC_CANDIDATE_ROOT}/include")
        set(ORBBEC_SDK_LIBRARY_DIR "${ORBBEC_CANDIDATE_ROOT}/lib")
      elseif(EXISTS "${ORBBEC_CANDIDATE_ROOT}/SDK/include/libobsensor/ObSensor.hpp")
        set(ORBBEC_SDK_INCLUDE_DIR "${ORBBEC_CANDIDATE_ROOT}/SDK/include")
        set(ORBBEC_SDK_LIBRARY_DIR "${ORBBEC_CANDIDATE_ROOT}/SDK/lib")
      endif()
    endforeach()

    if(NOT ORBBEC_SDK_INCLUDE_DIR OR NOT ORBBEC_SDK_LIBRARY_DIR)
      message(FATAL_ERROR "ENABLE_ORBBEC=ON 但未找到 OrbbecSDK。可放到 third_party/orbbec_sdk，或设置 -DORBBEC_SDK_ROOT=/path/to/OrbbecSDK_v*/SDK")
    endif()

    set(ORBBEC_SDK_LIBRARY "${ORBBEC_SDK_LIBRARY_DIR}/libOrbbecSDK.so")
    if(NOT EXISTS "${ORBBEC_SDK_LIBRARY}")
      message(FATAL_ERROR "未找到 OrbbecSDK 库: ${ORBBEC_SDK_LIBRARY}")
    endif()
  endif()
endif()

add_executable(rk3588_sop
  src/main.cpp
  src/time_utils.cpp
  src/config_loader.cpp
  src/geometry_utils.cpp
  src/object_tracker.cpp
  src/video_source.cpp
  src/yolov8_detector.cpp
  src/hand_pose_detector.cpp
  src/sop_state_machine.cpp
  src/visualizer.cpp
  third_party/hungarian_algorithm_cpp/Hungarian.cpp
)

target_include_directories(rk3588_sop
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/hungarian_algorithm_cpp
)

target_link_libraries(rk3588_sop PRIVATE Threads::Threads)

if(OpenCV_FOUND)
  target_compile_definitions(rk3588_sop PRIVATE RK3588_SOP_HAS_OPENCV=1)
  target_link_libraries(rk3588_sop PRIVATE ${OpenCV_LIBS})
else()
  target_compile_definitions(rk3588_sop PRIVATE RK3588_SOP_HAS_OPENCV=0)
endif()

if(ENABLE_RKNN)
  set(RKNN_ROOT "" CACHE PATH "RKNN runtime 根目录，包含 include/rknn_api.h 和 lib/librknnrt.so")
  find_path(RKNN_INCLUDE_DIR
    NAMES rknn_api.h
    PATHS
      ${RKNN_ROOT}/include
      ${RKNN_ROOT}/runtime/RK3588/Linux/librknn_api/include
      /usr/include
      /usr/local/include
      /usr/include/rknn
      /usr/local/include/rknn
  )
  find_library(RKNN_LIBRARY
    NAMES rknnrt librknnrt.so
    PATHS
      ${RKNN_ROOT}/lib
      ${RKNN_ROOT}/runtime/RK3588/Linux/librknn_api/aarch64
      /usr/lib
      /usr/local/lib
      /usr/lib/aarch64-linux-gnu
  )
  if(NOT RKNN_INCLUDE_DIR OR NOT RKNN_LIBRARY)
    message(FATAL_ERROR "ENABLE_RKNN=ON 但未找到 RKNN runtime，请设置 -DRKNN_ROOT=/path/to/rknpu2/runtime/RK3588/Linux/librknn_api")
  endif()
  target_compile_definitions(rk3588_sop PRIVATE RK3588_SOP_ENABLE_RKNN=1)
  target_include_directories(rk3588_sop PRIVATE ${RKNN_INCLUDE_DIR})
  target_link_libraries(rk3588_sop PRIVATE ${RKNN_LIBRARY})
else()
  target_compile_definitions(rk3588_sop PRIVATE RK3588_SOP_ENABLE_RKNN=0)
endif()

if(ENABLE_ORBBEC)
  target_compile_definitions(rk3588_sop PRIVATE RK3588_SOP_ENABLE_ORBBEC=1)
  if(OrbbecSDK_FOUND)
    target_link_libraries(rk3588_sop PRIVATE OrbbecSDK::OrbbecSDK)
  else()
    target_include_directories(rk3588_sop PRIVATE ${ORBBEC_SDK_INCLUDE_DIR})
    target_link_libraries(rk3588_sop PRIVATE ${ORBBEC_SDK_LIBRARY})
    target_link_directories(rk3588_sop PRIVATE ${ORBBEC_SDK_LIBRARY_DIR})
    set_target_properties(rk3588_sop PROPERTIES BUILD_RPATH "${ORBBEC_SDK_LIBRARY_DIR}")
  endif()
else()
  target_compile_definitions(rk3588_sop PRIVATE RK3588_SOP_ENABLE_ORBBEC=0)
endif()

target_compile_options(rk3588_sop PRIVATE -Wall -Wextra -Wpedantic)
