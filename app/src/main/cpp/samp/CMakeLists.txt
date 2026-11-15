cmake_minimum_required(VERSION 3.12)
project(samp)

set(CMAKE_CXX_STANDARD 20)

# Add source files
file(GLOB_RECURSE SOURCES
        ${CMAKE_CURRENT_LIST_DIR}/*.c*
        ${CMAKE_CURRENT_LIST_DIR}/**/*.c*
        ${CMAKE_CURRENT_LIST_DIR}/**/**/*.c*
        ${CMAKE_CURRENT_LIST_DIR}/**/**/**/*.c*
        ${CMAKE_CURRENT_LIST_DIR}/**/**/**/**/*.c*
        ${CMAKE_CURRENT_LIST_DIR}/**/**/**/**/**/*.c*
        ${CMAKE_CURRENT_LIST_DIR}/**/**/**/**/**/**/*.c*
        ${CMAKE_CURRENT_LIST_DIR}/**/**/**/**/**/**/**/*.c*
        ${CMAKE_CURRENT_LIST_DIR}/**/**/**/**/**/**/**/**/*.c*
        ${CMAKE_CURRENT_LIST_DIR}/**/**/**/**/**/**/**/**/**/*.c*
        )

# Add include directories
include_directories(
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/util/
        ${CMAKE_CURRENT_LIST_DIR}/vendor/armhook
        ${CMAKE_CURRENT_LIST_DIR}/vendor/imgui
        ${CMAKE_CURRENT_LIST_DIR}/vendor/
        ${CMAKE_CURRENT_LIST_DIR}/game/RW/
        ${CMAKE_CURRENT_LIST_DIR}/game/Core/
)

# Opus
#add_library(libopus STATIC IMPORTED)
#set_target_properties(libopus PROPERTIES IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/vendor/opus/libopus.a)

add_library(samp SHARED ${SOURCES})
target_link_libraries(samp log EGL GLESv3 opus shadowhook::shadowhook ${CMAKE_CURRENT_SOURCE_DIR}/vendor/bass/libs/${ANDROID_ABI}/libbass.so)
# Android 15/16: aparelhos com paginas de 16 KB exigem libs ELF alinhadas em 16 KB. O NDK r26 alinha em 4 KB por padrao.
target_link_options(samp PRIVATE -Wl,-z,max-page-size=16384)

# Set compilation flags
#
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -w -s -g -fvisibility=hidden -pthread -Wall -O3 -fexceptions")

