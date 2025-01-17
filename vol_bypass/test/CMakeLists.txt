cmake_minimum_required(VERSION 3.12)
project(Test_bypass_vol VERSION 0.0.1 LANGUAGES C)

if(NOT HDF5_BUILD)
  set(HDF5_BUILD ${CMAKE_INSTALL_PREFIX})
endif()

add_executable(h5_create h5_create.c)
add_executable(h5_read h5_read.c)
add_executable(posix_read_mthread posix_read_mthread.c)
add_executable(posix_read_tpool posix_read_tpool.c)

find_library(HDF5_LIBRARY
    NAMES hdf5 z dl m
    HINTS ${HDF5_BUILD}/lib)

if (HDF5_LIBRARY)
  message ("HDF5_BUILD is ${HDF5_BUILD}")
  message ("HDF5 library is ${HDF5_LIBRARY}")
else ()
  message ("HDF5 library not found")
endif ()

if (HDF5_LIBRARY)
    target_link_libraries(h5_create PRIVATE ${HDF5_LIBRARY})
    target_link_libraries(h5_read PRIVATE ${HDF5_LIBRARY})
endif()

target_include_directories(h5_create PRIVATE ${HDF5_BUILD}/include)
target_include_directories(h5_read PRIVATE ${HDF5_BUILD}/include)

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_options(posix_read_mthread PRIVATE -lpthread)
    target_compile_options(posix_read_tpool PRIVATE -lpthread)
elseif (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    # other macOS-specific flags for Clang
endif ()
