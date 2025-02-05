cmake_minimum_required(VERSION 3.12)
project(HDF5_VOL_BYPASS_TEST VERSION 0.0.1 LANGUAGES C)

add_executable(h5_create h5_create.c)
add_executable(h5_read h5_read.c)

target_link_libraries(h5_create PRIVATE ${HDF5_LIBRARIES})
target_link_libraries(h5_read PRIVATE ${HDF5_LIBRARIES})

target_include_directories(h5_create
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PUBLIC ${HDF5_INCLUDE_DIRS}
)

target_include_directories(h5_read
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PUBLIC ${HDF5_INCLUDE_DIRS}
)

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
elseif (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    # other macOS-specific flags for Clang
endif ()
