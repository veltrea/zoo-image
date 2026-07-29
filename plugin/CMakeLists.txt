# ZooImage — FileMaker plug-in (macOS .fmplugin bundle)
# Windows(.fmx64) / Linux(.fmx) は後続で追加。まずは macOS。
#
#   cmake -S plugin -B plugin/build -DCMAKE_BUILD_TYPE=Release
#   cmake --build plugin/build
#   → plugin/build/ZooImage.fmplugin

cmake_minimum_required(VERSION 3.16)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

project(ZooImage VERSION 0.1.0 LANGUAGES CXX)

if(NOT APPLE)
    message(FATAL_ERROR "This CMakeLists currently targets macOS. Windows/Linux support is planned.")
endif()

# FileMaker Plug-in SDK の場所（既定は本リポジトリの隣。-DFM_SDK_DIR=... で上書き可）。
set(FM_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../PlugInSDK" CACHE PATH "FileMaker Plug-in SDK root")
set(FM_HEADERS "${FM_SDK_DIR}/Headers")
set(FM_FRAMEWORKS "${FM_SDK_DIR}/Libraries/Mac")

if(NOT EXISTS "${FM_HEADERS}/FMWrapper/FMXExtern.h")
    message(FATAL_ERROR "FileMaker SDK headers not found under ${FM_HEADERS}. Set -DFM_SDK_DIR=<PlugInSDK>.")
endif()

# ユニバーサルバイナリ（FMWrapper.framework は x86_64+arm64）。
if(NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
endif()
if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "" FORCE)
endif()

set(ZOOIMAGE_SOURCES
    src/ZooImage.cpp
    src/helper_client.cpp
    src/container.cpp
    src/event_queue.cpp
)

add_library(ZooImage MODULE ${ZOOIMAGE_SOURCES})

target_include_directories(ZooImage PRIVATE
    ${FM_HEADERS}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

# 既定 visibility を維持する（-fvisibility=hidden にすると FMExternCallProc が
# 隠れて FileMaker がエントリポイントを解決できなくなる）。
target_compile_options(ZooImage PRIVATE
    -fexceptions
    -fPIC
    -Wall
    -Wno-unused-parameter
)

# FileMaker 拡張バンドル。
set_target_properties(ZooImage PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION "fmplugin"
    OUTPUT_NAME "ZooImage"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
)

# FMWrapper.framework は weak リンク（実行時は FileMaker 本体のものを使う）。
target_link_options(ZooImage PRIVATE
    "-F${FM_FRAMEWORKS}"
    "-weak_framework" "FMWrapper"
)
