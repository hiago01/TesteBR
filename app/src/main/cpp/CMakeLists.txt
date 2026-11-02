cmake_minimum_required(VERSION 3.12)

include_directories(
        ${CMAKE_CURRENT_LIST_DIR}/opus/include
)

# Android 15+ em aparelhos com paginas de 16 KB: segmentos ELF alinhados em 16 KB (NDK 26 usa 4 KB por padrao).
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,-z,max-page-size=16384 -Wl,-z,common-page-size=16384")

find_package(shadowhook REQUIRED CONFIG)
add_subdirectory(opus)
add_subdirectory(samp)

target_compile_options(opus PRIVATE -O3)
