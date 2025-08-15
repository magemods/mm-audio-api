cmake_minimum_required(VERSION 3.20)

project(recomp_mod_native_lib)

set(TARGET_NAME ${LIB_NAME})
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# deps
add_subdirectory(thirdparty/plog)
add_subdirectory(thirdparty/miniz)
add_subdirectory(thirdparty/ogg)

# vorbis
set(OGG_LIBRARY ogg)
set(OGG_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/thirdparty/ogg/include)
add_subdirectory(thirdparty/vorbis)

# opus
set(OP_DISABLE_HTTP ON)
set(OP_DISABLE_EXAMPLES ON)
set(OP_DISABLE_DOCS ON)
add_subdirectory(thirdparty/opus)
add_subdirectory(thirdparty/opusfile)

add_library(${TARGET_NAME} SHARED)

target_compile_features(${TARGET_NAME} PRIVATE cxx_std_23)

target_link_libraries(${TARGET_NAME}
    PRIVATE
        miniz
        ogg
        vorbis
        vorbisfile
        opus
        opusfile
)

target_include_directories(${TARGET_NAME}
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src/extlib
        ${CMAKE_SOURCE_DIR}/offline_build
        ${CMAKE_SOURCE_DIR}/include/extlib
        ${CMAKE_SOURCE_DIR}/include/shared
        ${CMAKE_SOURCE_DIR}/thirdparty/plog/include
        ${CMAKE_SOURCE_DIR}/thirdparty/dr_libs
        ${CMAKE_SOURCE_DIR}/thirdparty/ogg/include
        ${CMAKE_SOURCE_DIR}/thirdparty/vorbis/include
        ${CMAKE_SOURCE_DIR}/thirdparty/opus/include
        ${CMAKE_SOURCE_DIR}/thirdparty/opusfile/include
)

add_subdirectory(src/extlib)

set_target_properties(${TARGET_NAME}
    PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/arc
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
