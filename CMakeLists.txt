cmake_minimum_required(VERSION 3.10)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(NOT DEFINED ENV{KOS_BASE})
    message(FATAL_ERROR "Please define KOS_BASE to point to your SDK path!")
  endif()
endif()

project(APOLLO-DC)

find_package(CURL REQUIRED)

option(APOLLO_ENABLE_LOGGING "enables debug logging over UDP multicast" OFF)

if(APOLLO_ENABLE_LOGGING)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DAPOLLO_ENABLE_LOGGING")
endif()

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=gnu11")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -s")

include_directories(
  include
  $ENV{KOS_PORTS}/include/freetype2
  $ENV{KOS_PORTS}/include/zlib
  $ENV{KOS_PORTS}/include/libzip
)

link_directories(
  libs
)

# From vita shell CMakeLists - credits TheFlow
FUNCTION(ADD_RESOURCES out_var)
    SET(result)
    FOREACH(in_f ${ARGN})
        SET(out_f "${CMAKE_CURRENT_BINARY_DIR}/${in_f}.o")
        GET_FILENAME_COMPONENT(out_dir ${out_f} DIRECTORY)
        ADD_CUSTOM_COMMAND(OUTPUT ${out_f}
                COMMAND ${CMAKE_COMMAND} -E make_directory ${out_dir}
                COMMAND $ENV{KOS_BASE}/utils/bin2o/bin2o ${in_f} bsym${in_f} ${out_f}
                DEPENDS ${in_f}
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                COMMENT "Building resource ${out_f}"
                VERBATIM
                )
        LIST(APPEND result ${out_f})
    ENDFOREACH()
    SET(${out_var} "${result}" PARENT_SCOPE)
ENDFUNCTION()

file(GLOB res_files RELATIVE
  ${CMAKE_SOURCE_DIR}
  data/*.png
  data/*.s3m
)
add_resources(apollo_res ${res_files})

add_executable(${PROJECT_NAME}
  ${apollo_res}
  source/common.c
  source/config.c
  source/dialog.c
  source/draw.c
  source/exec_cmd.c
  source/libfont.c
  source/main.c
  source/menu_hex.c
  source/menu_about.c
  source/menu_cheats.c
  source/menu_options.c
  source/menu_main.c
  source/saves.c
  source/settings.c
  source/sjis.c
  source/ttf_fonts.c
  source/util.c
  source/dcpad.c
  source/dcsave.c
  source/dcvmu.c
  source/vmu_dev.c
  source/vmu_card.c
  source/zip_util.c
)

target_link_libraries(${PROJECT_NAME}
  CURL::libcurl
  SDL2
  pthread
  GL
  apollo
  polarssl
  dbglogger
  freetype
  png
  bz2
  zip
  kosfat
  z
)

# Self-booting disc image: the app, plus app data and the offline Saves DB (see tools/make_disc.sh)
set(APOLLO_SAVES_DIR "${CMAKE_SOURCE_DIR}/../apollo-saves" CACHE PATH "apollo-saves checkout (uses its DC/ folder)")
set(APOLLO_PATCHES_DIR "${CMAKE_SOURCE_DIR}/../apollo-patches" CACHE PATH "apollo-patches checkout")
set(MKDCDISC "mkdcdisc" CACHE FILEPATH "mkdcdisc executable")

add_custom_target(cdi
  COMMAND ${CMAKE_COMMAND} -E env MKDCDISC=${MKDCDISC}
          sh ${CMAKE_SOURCE_DIR}/tools/make_disc.sh $<TARGET_FILE:${PROJECT_NAME}> ${APOLLO_SAVES_DIR} ${APOLLO_PATCHES_DIR} apollo-dc.cdi
  DEPENDS ${PROJECT_NAME}
  COMMENT "Building apollo-dc.cdi"
  VERBATIM
)
